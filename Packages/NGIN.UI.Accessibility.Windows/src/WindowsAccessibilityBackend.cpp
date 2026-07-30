#include <NGIN/UI/Accessibility/Windows/Windows.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Unknwn.h>
#include <OleAuto.h>
#include <UIAutomation.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

namespace NGIN::UI::Accessibility::Windows {
namespace {
constexpr UINT_PTR SubclassIdentifier = 0x4E47494EU;

struct WindowState final : std::enable_shared_from_this<WindowState> {
  HWND hwnd{};
  PlatformWindowHandle window{};
  IAccessibilityActionSink *sink{};
  std::mutex mutex{};
  AccessibilitySnapshot snapshot{};
  std::atomic_bool alive{true};
  std::atomic<UInt64> postedActions{0};
  std::atomic<UInt64> failedActions{0};
};

struct WindowHandleHash final {
  [[nodiscard]] auto operator()(const PlatformWindowHandle value) const noexcept
      -> std::size_t {
    return (static_cast<std::size_t>(value.index) << 32U) ^
           static_cast<std::size_t>(value.generation);
  }
};

[[nodiscard]] auto FindNode(const std::shared_ptr<WindowState> &state,
                            const SemanticNodeId id,
                            SemanticNode &node) noexcept -> HRESULT {
  if (!state || !state->alive.load()) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  std::scoped_lock lock{state->mutex};
  const auto *found = state->snapshot.Find(id);
  if (found == nullptr) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  node = *found;
  return S_OK;
}

[[nodiscard]] auto ToWide(const NGIN::Text::String &text) -> std::wstring {
  if (text.Empty()) {
    return {};
  }
  const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          text.CStr(), -1, nullptr, 0);
  if (length <= 1) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.CStr(), -1,
                      result.data(), length);
  result.pop_back();
  return result;
}

[[nodiscard]] auto ToUtf8(const wchar_t *text) -> NGIN::Text::String {
  if (text == nullptr || *text == L'\0') {
    return {};
  }
  const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                                          -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) {
    return {};
  }
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(),
                      length, nullptr, nullptr);
  result.pop_back();
  return NGIN::Text::String{result};
}

void SetString(VARIANT *value, const NGIN::Text::String &text) {
  VariantInit(value);
  value->vt = VT_BSTR;
  const auto wide = ToWide(text);
  value->bstrVal =
      SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
}

void SetBool(VARIANT *value, const bool state) noexcept {
  VariantInit(value);
  value->vt = VT_BOOL;
  value->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
}

void SetInt(VARIANT *value, const int number) noexcept {
  VariantInit(value);
  value->vt = VT_I4;
  value->lVal = number;
}

void SetDouble(VARIANT *value, const double number) noexcept {
  VariantInit(value);
  value->vt = VT_R8;
  value->dblVal = number;
}

[[nodiscard]] auto ControlType(const SemanticRole role) noexcept
    -> CONTROLTYPEID {
  switch (role) {
  case SemanticRole::Window:
    return UIA_WindowControlTypeId;
  case SemanticRole::Group:
  case SemanticRole::TabPanel:
    return UIA_GroupControlTypeId;
  case SemanticRole::Heading:
  case SemanticRole::Text:
    return UIA_TextControlTypeId;
  case SemanticRole::Button:
    return UIA_ButtonControlTypeId;
  case SemanticRole::CheckBox:
    return UIA_CheckBoxControlTypeId;
  case SemanticRole::RadioButton:
    return UIA_RadioButtonControlTypeId;
  case SemanticRole::Switch:
    return UIA_CheckBoxControlTypeId;
  case SemanticRole::TextBox:
    return UIA_EditControlTypeId;
  case SemanticRole::List:
    return UIA_ListControlTypeId;
  case SemanticRole::ListItem:
    return UIA_ListItemControlTypeId;
  case SemanticRole::Image:
    return UIA_ImageControlTypeId;
  case SemanticRole::Link:
    return UIA_HyperlinkControlTypeId;
  case SemanticRole::Slider:
    return UIA_SliderControlTypeId;
  case SemanticRole::ProgressBar:
    return UIA_ProgressBarControlTypeId;
  case SemanticRole::ComboBox:
    return UIA_ComboBoxControlTypeId;
  case SemanticRole::TabList:
    return UIA_TabControlTypeId;
  case SemanticRole::Tab:
    return UIA_TabItemControlTypeId;
  case SemanticRole::Menu:
    return UIA_MenuControlTypeId;
  case SemanticRole::MenuItem:
    return UIA_MenuItemControlTypeId;
  case SemanticRole::Dialog:
    return UIA_WindowControlTypeId;
  case SemanticRole::None:
    return UIA_CustomControlTypeId;
  }
  return UIA_CustomControlTypeId;
}

class ElementProvider final
    : public RuntimeClass<
          RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
          IRawElementProviderFragment, IRawElementProviderFragmentRoot,
          IInvokeProvider, IToggleProvider, IRangeValueProvider, IValueProvider,
          ISelectionItemProvider, IExpandCollapseProvider, IScrollItemProvider,
          IVirtualizedItemProvider> {
public:
  ElementProvider(std::shared_ptr<WindowState> state,
                  const SemanticNodeId id) noexcept
      : m_state(std::move(state)), m_id(id) {}

  IFACEMETHODIMP get_ProviderOptions(ProviderOptions *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                          ProviderOptions_UseComThreading);
    return S_OK;
  }

  IFACEMETHODIMP GetPatternProvider(PATTERNID pattern,
                                    IUnknown **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }

    const bool invoke =
        pattern == UIA_InvokePatternId &&
        HasSemanticAction(node.actions, SemanticActionFlags::Activate);
    const bool toggle = pattern == UIA_TogglePatternId &&
                        (node.role == SemanticRole::CheckBox ||
                         node.role == SemanticRole::Switch);
    const bool range =
        pattern == UIA_RangeValuePatternId && node.range.has_value();
    const bool textValue =
        pattern == UIA_ValuePatternId && node.role == SemanticRole::TextBox;
    const bool selection =
        pattern == UIA_SelectionItemPatternId &&
        HasSemanticAction(node.actions, SemanticActionFlags::Select);
    const bool expand =
        pattern == UIA_ExpandCollapsePatternId &&
        (HasSemanticAction(node.actions, SemanticActionFlags::Expand) ||
         HasSemanticAction(node.actions, SemanticActionFlags::Collapse));
    const bool scroll =
        pattern == UIA_ScrollItemPatternId &&
        HasSemanticAction(node.actions, SemanticActionFlags::ScrollIntoView);
    const bool virtualized =
        pattern == UIA_VirtualizedItemPatternId &&
        (HasSemanticAction(node.actions, SemanticActionFlags::Realize) ||
         HasSemanticState(node.states, SemanticStateFlags::Virtualized));
    if (!(invoke || toggle || range || textValue || selection || expand ||
          scroll || virtualized)) {
      return S_OK;
    }

    if (invoke) {
      return QueryInterface(IID_IInvokeProvider,
                            reinterpret_cast<void **>(value));
    }
    if (toggle) {
      return QueryInterface(IID_IToggleProvider,
                            reinterpret_cast<void **>(value));
    }
    if (range) {
      return QueryInterface(IID_IRangeValueProvider,
                            reinterpret_cast<void **>(value));
    }
    if (textValue) {
      return QueryInterface(IID_IValueProvider,
                            reinterpret_cast<void **>(value));
    }
    if (selection) {
      return QueryInterface(IID_ISelectionItemProvider,
                            reinterpret_cast<void **>(value));
    }
    if (expand) {
      return QueryInterface(IID_IExpandCollapseProvider,
                            reinterpret_cast<void **>(value));
    }
    if (scroll) {
      return QueryInterface(IID_IScrollItemProvider,
                            reinterpret_cast<void **>(value));
    }
    return QueryInterface(IID_IVirtualizedItemProvider,
                          reinterpret_cast<void **>(value));
  }

  IFACEMETHODIMP GetPropertyValue(PROPERTYID property,
                                  VARIANT *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    VariantInit(value);
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    switch (property) {
    case UIA_ControlTypePropertyId:
      SetInt(value, ControlType(node.role));
      break;
    case UIA_NamePropertyId:
      SetString(value, node.label);
      break;
    case UIA_HelpTextPropertyId:
      SetString(value, node.description);
      break;
    case UIA_ValueValuePropertyId:
      SetString(value, node.password ? NGIN::Text::String{} : node.value);
      break;
    case UIA_RangeValueValuePropertyId:
      if (node.range) {
        SetDouble(value, node.range->current);
      }
      break;
    case UIA_RangeValueMinimumPropertyId:
      if (node.range) {
        SetDouble(value, node.range->minimum);
      }
      break;
    case UIA_RangeValueMaximumPropertyId:
      if (node.range) {
        SetDouble(value, node.range->maximum);
      }
      break;
    case UIA_RangeValueSmallChangePropertyId:
    case UIA_RangeValueLargeChangePropertyId:
      if (node.range) {
        SetDouble(value, node.range->step);
      }
      break;
    case UIA_ToggleToggleStatePropertyId:
      SetInt(value,
             HasSemanticState(node.states, SemanticStateFlags::Indeterminate)
                 ? ToggleState_Indeterminate
             : HasSemanticState(node.states, SemanticStateFlags::Checked)
                 ? ToggleState_On
                 : ToggleState_Off);
      break;
    case UIA_SelectionItemIsSelectedPropertyId:
      SetBool(value,
              HasSemanticState(node.states, SemanticStateFlags::Selected));
      break;
    case UIA_ExpandCollapseExpandCollapseStatePropertyId:
      SetInt(value, HasSemanticState(node.states, SemanticStateFlags::Expanded)
                        ? ExpandCollapseState_Expanded
                        : ExpandCollapseState_Collapsed);
      break;
    case UIA_AutomationIdPropertyId:
      SetString(value, node.identifier);
      break;
    case UIA_FrameworkIdPropertyId:
      SetString(value, NGIN::Text::String{"NGIN.UI"});
      break;
    case UIA_IsEnabledPropertyId:
      SetBool(value,
              !HasSemanticState(node.states, SemanticStateFlags::Disabled));
      break;
    case UIA_HasKeyboardFocusPropertyId:
      SetBool(value,
              HasSemanticState(node.states, SemanticStateFlags::Focused));
      break;
    case UIA_IsKeyboardFocusablePropertyId:
      SetBool(value,
              HasSemanticAction(node.actions, SemanticActionFlags::Focus));
      break;
    case UIA_IsPasswordPropertyId:
      SetBool(value, node.password);
      break;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
      SetBool(value, true);
      break;
    case UIA_IsOffscreenPropertyId:
      SetBool(value, node.bounds.width <= 0.0F || node.bounds.height <= 0.0F);
      break;
    case UIA_PositionInSetPropertyId:
      if (node.collectionItem) {
        SetInt(value, static_cast<int>(node.collectionItem->position));
      }
      break;
    case UIA_SizeOfSetPropertyId:
      if (node.collectionItem) {
        SetInt(value, static_cast<int>(node.collectionItem->count));
      }
      break;
    case UIA_LevelPropertyId:
      if (node.collectionItem) {
        SetInt(value, static_cast<int>(node.collectionItem->level));
      }
      break;
    case UIA_LiveSettingPropertyId:
      SetInt(value, node.live == SemanticLiveSetting::Assertive ? Assertive
                    : node.live == SemanticLiveSetting::Polite  ? Polite
                                                                : Off);
      break;
    default:
      break;
    }
    return S_OK;
  }

  IFACEMETHODIMP get_HostRawElementProvider(
      IRawElementProviderSimple **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    if (node.parent.IsValid()) {
      return S_OK;
    }
    return UiaHostProviderFromHwnd(m_state->hwnd, value);
  }

  IFACEMETHODIMP
  Navigate(NavigateDirection direction,
           IRawElementProviderFragment **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNode node;
    auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    SemanticNodeId target{};
    if (direction == NavigateDirection_Parent) {
      target = node.parent;
    } else if (direction == NavigateDirection_FirstChild &&
               !node.children.empty()) {
      target = node.children.front();
    } else if (direction == NavigateDirection_LastChild &&
               !node.children.empty()) {
      target = node.children.back();
    } else if ((direction == NavigateDirection_NextSibling ||
                direction == NavigateDirection_PreviousSibling) &&
               node.parent.IsValid()) {
      SemanticNode parent;
      status = FindNode(m_state, node.parent, parent);
      if (FAILED(status)) {
        return status;
      }
      const auto item =
          std::find(parent.children.begin(), parent.children.end(), m_id);
      if (item != parent.children.end()) {
        if (direction == NavigateDirection_NextSibling &&
            std::next(item) != parent.children.end()) {
          target = *std::next(item);
        } else if (direction == NavigateDirection_PreviousSibling &&
                   item != parent.children.begin()) {
          target = *std::prev(item);
        }
      }
    }
    return MakeFragment(target, value);
  }

  IFACEMETHODIMP GetRuntimeId(SAFEARRAY **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    if (!node.parent.IsValid()) {
      return S_OK;
    }
    int ids[] = {UiaAppendRuntimeId, static_cast<int>(m_state->window.index),
                 static_cast<int>(m_id.value & 0xFFFFFFFFULL),
                 static_cast<int>((m_id.value >> 32U) & 0xFFFFFFFFULL)};
    auto *array = SafeArrayCreateVector(VT_I4, 0, 4);
    if (array == nullptr) {
      return E_OUTOFMEMORY;
    }
    for (LONG index = 0; index < 4; ++index) {
      if (FAILED(SafeArrayPutElement(array, &index, &ids[index]))) {
        SafeArrayDestroy(array);
        return E_FAIL;
      }
    }
    *value = array;
    return S_OK;
  }

  IFACEMETHODIMP get_BoundingRectangle(UiaRect *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    const auto scale =
        static_cast<double>(GetDpiForWindow(m_state->hwnd)) / 96.0;
    POINT origin{};
    ClientToScreen(m_state->hwnd, &origin);
    value->left = origin.x + static_cast<double>(node.bounds.x) * scale;
    value->top = origin.y + static_cast<double>(node.bounds.y) * scale;
    value->width = static_cast<double>(node.bounds.width) * scale;
    value->height = static_cast<double>(node.bounds.height) * scale;
    return S_OK;
  }

  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    return S_OK;
  }

  IFACEMETHODIMP SetFocus() noexcept override {
    return Post(SemanticActionKind::Focus);
  }

  IFACEMETHODIMP
  get_FragmentRoot(IRawElementProviderFragmentRoot **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNodeId root{};
    {
      if (!m_state || !m_state->alive.load()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
      }
      std::scoped_lock lock{m_state->mutex};
      root = m_state->snapshot.root;
    }
    if (!root.IsValid()) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    auto provider = Make<ElementProvider>(m_state, root);
    if (!provider) {
      return E_OUTOFMEMORY;
    }
    return provider.CopyTo(value);
  }

  IFACEMETHODIMP ElementProviderFromPoint(
      double x, double y,
      IRawElementProviderFragment **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    if (!m_state || !m_state->alive.load()) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    POINT origin{};
    ClientToScreen(m_state->hwnd, &origin);
    const auto scale =
        static_cast<double>(GetDpiForWindow(m_state->hwnd)) / 96.0;
    const auto logicalX = (x - origin.x) / scale;
    const auto logicalY = (y - origin.y) / scale;
    SemanticNodeId target{};
    {
      std::scoped_lock lock{m_state->mutex};
      for (auto item = m_state->snapshot.nodes.rbegin();
           item != m_state->snapshot.nodes.rend(); ++item) {
        if (logicalX >= item->bounds.x && logicalY >= item->bounds.y &&
            logicalX <= item->bounds.x + item->bounds.width &&
            logicalY <= item->bounds.y + item->bounds.height) {
          target = item->id;
          break;
        }
      }
      if (!target.IsValid()) {
        target = m_state->snapshot.root;
      }
    }
    return MakeFragment(target, value);
  }

  IFACEMETHODIMP
  GetFocus(IRawElementProviderFragment **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    if (!m_state || !m_state->alive.load()) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    SemanticNodeId focused{};
    {
      std::scoped_lock lock{m_state->mutex};
      focused = m_state->snapshot.focused;
    }
    return MakeFragment(focused, value);
  }

  IFACEMETHODIMP Invoke() noexcept override {
    return Post(SemanticActionKind::Activate);
  }

  IFACEMETHODIMP Toggle() noexcept override {
    return Post(SemanticActionKind::Activate);
  }

  IFACEMETHODIMP get_ToggleState(ToggleState *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    *value = HasSemanticState(node.states, SemanticStateFlags::Indeterminate)
                 ? ToggleState_Indeterminate
             : HasSemanticState(node.states, SemanticStateFlags::Checked)
                 ? ToggleState_On
                 : ToggleState_Off;
    return S_OK;
  }

  IFACEMETHODIMP SetValue(double value) noexcept override {
    return Post(SemanticActionKind::SetValue, {}, value);
  }

  IFACEMETHODIMP get_Value(double *value) noexcept override {
    return RangePart(value, 0);
  }

  IFACEMETHODIMP get_IsReadOnly(BOOL *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    *value =
        HasSemanticState(node.states, SemanticStateFlags::ReadOnly) ||
                !HasSemanticAction(node.actions, SemanticActionFlags::SetValue)
            ? TRUE
            : FALSE;
    return S_OK;
  }

  IFACEMETHODIMP get_Maximum(double *value) noexcept override {
    return RangePart(value, 1);
  }
  IFACEMETHODIMP get_Minimum(double *value) noexcept override {
    return RangePart(value, 2);
  }
  IFACEMETHODIMP get_LargeChange(double *value) noexcept override {
    return RangePart(value, 3);
  }
  IFACEMETHODIMP get_SmallChange(double *value) noexcept override {
    return RangePart(value, 3);
  }

  IFACEMETHODIMP SetValue(LPCWSTR value) noexcept override {
    return Post(SemanticActionKind::SetValue, ToUtf8(value));
  }

  IFACEMETHODIMP get_Value(BSTR *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    const auto wide = node.password ? std::wstring{} : ToWide(node.value);
    *value = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
    return *value != nullptr || wide.empty() ? S_OK : E_OUTOFMEMORY;
  }

  IFACEMETHODIMP Select() noexcept override {
    return Post(SemanticActionKind::Select);
  }
  IFACEMETHODIMP AddToSelection() noexcept override {
    return Post(SemanticActionKind::Select);
  }
  IFACEMETHODIMP RemoveFromSelection() noexcept override {
    return UIA_E_INVALIDOPERATION;
  }
  IFACEMETHODIMP get_IsSelected(BOOL *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    *value = HasSemanticState(node.states, SemanticStateFlags::Selected)
                 ? TRUE
                 : FALSE;
    return S_OK;
  }
  IFACEMETHODIMP
  get_SelectionContainer(IRawElementProviderSimple **value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    *value = nullptr;
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    if (!node.parent.IsValid()) {
      return S_OK;
    }
    auto provider = Make<ElementProvider>(m_state, node.parent);
    if (!provider) {
      return E_OUTOFMEMORY;
    }
    return provider.CopyTo(value);
  }

  IFACEMETHODIMP Expand() noexcept override {
    return Post(SemanticActionKind::Expand);
  }
  IFACEMETHODIMP Collapse() noexcept override {
    return Post(SemanticActionKind::Collapse);
  }
  IFACEMETHODIMP
  get_ExpandCollapseState(ExpandCollapseState *value) noexcept override {
    if (value == nullptr) {
      return E_POINTER;
    }
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    *value = HasSemanticState(node.states, SemanticStateFlags::Expanded)
                 ? ExpandCollapseState_Expanded
                 : ExpandCollapseState_Collapsed;
    return S_OK;
  }

  IFACEMETHODIMP ScrollIntoView() noexcept override {
    return Post(SemanticActionKind::ScrollIntoView);
  }

  IFACEMETHODIMP Realize() noexcept override {
    return Post(SemanticActionKind::Realize);
  }

private:
  auto MakeFragment(const SemanticNodeId id,
                    IRawElementProviderFragment **value) noexcept -> HRESULT {
    if (!id.IsValid()) {
      return S_OK;
    }
    auto provider = Make<ElementProvider>(m_state, id);
    if (!provider) {
      return E_OUTOFMEMORY;
    }
    return provider.CopyTo(value);
  }

  auto Post(const SemanticActionKind action, NGIN::Text::String value = {},
            const F64 numericValue = 0.0) noexcept -> HRESULT {
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    if (HasSemanticState(node.states, SemanticStateFlags::Disabled)) {
      return UIA_E_ELEMENTNOTENABLED;
    }
    if (m_state->sink == nullptr) {
      ++m_state->failedActions;
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    auto result =
        m_state->sink->PostAccessibilityAction(AccessibilityActionRequest{
            .window = m_state->window,
            .semantic =
                SemanticActionRequest{
                    .node = m_id,
                    .action = action,
                    .value = std::move(value),
                    .numericValue = numericValue,
                },
        });
    if (result.HasValue()) {
      ++m_state->postedActions;
      return S_OK;
    }
    ++m_state->failedActions;
    return UIA_E_INVALIDOPERATION;
  }

  auto RangePart(double *value, const int part) noexcept -> HRESULT {
    if (value == nullptr) {
      return E_POINTER;
    }
    SemanticNode node;
    const auto status = FindNode(m_state, m_id, node);
    if (FAILED(status)) {
      return status;
    }
    if (!node.range) {
      return UIA_E_INVALIDOPERATION;
    }
    switch (part) {
    case 0:
      *value = node.range->current;
      break;
    case 1:
      *value = node.range->maximum;
      break;
    case 2:
      *value = node.range->minimum;
      break;
    default:
      *value = node.range->step;
      break;
    }
    return S_OK;
  }

  std::shared_ptr<WindowState> m_state;
  SemanticNodeId m_id{};
};

[[nodiscard]] auto RootProvider(const std::shared_ptr<WindowState> &state)
    -> ComPtr<ElementProvider> {
  SemanticNodeId root{};
  {
    std::scoped_lock lock{state->mutex};
    root = state->snapshot.root;
  }
  return root.IsValid() ? Make<ElementProvider>(state, root) : nullptr;
}

LRESULT CALLBACK WindowSubclass(HWND hwnd, UINT message, WPARAM wParam,
                                LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
  auto *state = reinterpret_cast<WindowState *>(reference);
  if (message == WM_GETOBJECT && lParam == UiaRootObjectId &&
      state != nullptr && state->alive.load()) {
    try {
      auto provider = RootProvider(state->shared_from_this());
      if (provider) {
        return UiaReturnRawElementProvider(hwnd, wParam, lParam,
                                           provider.Get());
      }
    } catch (...) {
    }
  } else if (message == WM_DESTROY && state != nullptr) {
    state->alive.store(false);
    UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

class WindowsAccessibilityBackend final : public IAccessibilityBackend {
public:
  ~WindowsAccessibilityBackend() override {
    std::scoped_lock lock{m_mutex};
    for (const auto &[_, state] : m_windows) {
      RemoveWindowSubclass(state->hwnd, WindowSubclass, SubclassIdentifier);
      state->alive.store(false);
      UiaReturnRawElementProvider(state->hwnd, 0, 0, nullptr);
    }
  }

  [[nodiscard]] auto Name() const noexcept -> const char * override {
    return "Windows UI Automation";
  }
  [[nodiscard]] auto Capabilities() const noexcept
      -> AccessibilityCapabilityFlags override {
    return AccessibilityCapabilityFlags::NativeBridge |
           AccessibilityCapabilityFlags::Actions |
           AccessibilityCapabilityFlags::Events |
           AccessibilityCapabilityFlags::MultipleWindows |
           AccessibilityCapabilityFlags::VirtualizedItems;
  }

  auto Initialize(IAccessibilityActionSink &sink) noexcept
      -> UIResult<void> override {
    std::scoped_lock lock{m_mutex};
    m_sink = &sink;
    m_diagnostics.configured = true;
    m_diagnostics.available = true;
    m_diagnostics.providerName = NGIN::Text::String{Name()};
    m_diagnostics.capabilities = Capabilities();
    return {};
  }

  auto AttachWindow(const AccessibilityWindowInfo &info) noexcept
      -> UIResult<void> override {
    if (info.nativeWindow.kind != NativeWindowKind::Win32 ||
        info.nativeWindow.value == 0) {
      return Failure(UIErrorCode::Unsupported,
                     "The accessibility provider requires a Win32 HWND",
                     "AttachWindow");
    }
    auto *hwnd = reinterpret_cast<HWND>(info.nativeWindow.value);
    if (!IsWindow(hwnd)) {
      return Failure(UIErrorCode::InvalidArgument,
                     "The native accessibility window is not valid",
                     "AttachWindow");
    }
    std::shared_ptr<WindowState> state;
    try {
      state = std::make_shared<WindowState>();
    } catch (...) {
      return Failure(UIErrorCode::OutOfMemory,
                     "Could not allocate accessibility window state",
                     "AttachWindow");
    }
    state->hwnd = hwnd;
    state->window = info.window;
    state->sink = m_sink;
    if (!SetWindowSubclass(hwnd, WindowSubclass, SubclassIdentifier,
                           reinterpret_cast<DWORD_PTR>(state.get()))) {
      return Failure(UIErrorCode::BackendUnavailable,
                     "Could not attach the UI Automation window provider",
                     "AttachWindow", static_cast<Int32>(GetLastError()));
    }
    std::scoped_lock lock{m_mutex};
    m_windows[info.window] = std::move(state);
    m_diagnostics.attachedWindowCount = m_windows.size();
    return {};
  }

  auto DetachWindow(const PlatformWindowHandle window) noexcept
      -> UIResult<void> override {
    std::shared_ptr<WindowState> state;
    {
      std::scoped_lock lock{m_mutex};
      const auto item = m_windows.find(window);
      if (item == m_windows.end()) {
        return {};
      }
      state = std::move(item->second);
      m_windows.erase(item);
      m_diagnostics.attachedWindowCount = m_windows.size();
    }
    RemoveWindowSubclass(state->hwnd, WindowSubclass, SubclassIdentifier);
    state->alive.store(false);
    UiaReturnRawElementProvider(state->hwnd, 0, 0, nullptr);
    {
      std::scoped_lock lock{m_mutex};
      m_diagnostics.postedActionCount += state->postedActions.load();
      m_diagnostics.failedActionCount += state->failedActions.load();
    }
    return {};
  }

  auto Publish(AccessibilitySnapshot snapshot) noexcept
      -> UIResult<void> override {
    std::shared_ptr<WindowState> state;
    {
      std::scoped_lock lock{m_mutex};
      const auto item = m_windows.find(snapshot.window);
      if (item != m_windows.end()) {
        state = item->second;
      }
    }
    if (!state) {
      return Failure(UIErrorCode::InvalidState,
                     "Accessibility snapshot targets a detached window",
                     "Publish");
    }

    AccessibilitySnapshot previous;
    AccessibilitySnapshot current;
    try {
      std::scoped_lock lock{state->mutex};
      previous = state->snapshot;
      state->snapshot = std::move(snapshot);
      current = state->snapshot;
    } catch (...) {
      return Failure(UIErrorCode::OutOfMemory,
                     "Could not retain the accessibility snapshot", "Publish");
    }

    auto diff = DiffAccessibilitySnapshots(previous, current);
    RaiseEvents(state, previous, current, diff);
    std::scoped_lock lock{m_mutex};
    ++m_diagnostics.publishedSnapshotCount;
    return {};
  }

  [[nodiscard]] auto Diagnostics() const noexcept
      -> AccessibilityDiagnostics override {
    std::scoped_lock lock{m_mutex};
    auto result = m_diagnostics;
    for (const auto &[_, state] : m_windows) {
      result.postedActionCount += state->postedActions.load();
      result.failedActionCount += state->failedActions.load();
    }
    return result;
  }

private:
  auto Failure(const UIErrorCode code, const char *message,
               const char *operation, const Int32 nativeCode = 0) noexcept
      -> UIResult<void> {
    auto error = MakeUIError(code, message, Name(), operation, "", nativeCode);
    std::scoped_lock lock{m_mutex};
    m_diagnostics.lastError = error;
    return error;
  }

  void CountEvent(const HRESULT status) noexcept {
    std::scoped_lock lock{m_mutex};
    if (SUCCEEDED(status)) {
      ++m_diagnostics.raisedEventCount;
    } else {
      m_diagnostics.lastError =
          MakeUIError(UIErrorCode::BackendUnavailable,
                      "Windows UI Automation rejected a provider event", Name(),
                      "RaiseEvent", "", static_cast<Int32>(status));
    }
  }

  void RaiseEvents(const std::shared_ptr<WindowState> &state,
                   const AccessibilitySnapshot &previous,
                   const AccessibilitySnapshot &current,
                   const AccessibilitySnapshotDiff &diff) noexcept {
    if (previous.revision == 0 || diff.Empty()) {
      return;
    }
    auto root = RootProvider(state);
    if (!root) {
      return;
    }
    if (diff.structureChanged) {
      CountEvent(UiaRaiseStructureChangedEvent(
          root.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0));
    }
    if (diff.focus.IsValid()) {
      auto focused = Make<ElementProvider>(state, diff.focus);
      if (focused) {
        CountEvent(UiaRaiseAutomationEvent(focused.Get(),
                                           UIA_AutomationFocusChangedEventId));
      }
    }
    for (const auto &change : diff.changed) {
      auto provider = Make<ElementProvider>(state, change.node);
      const auto *oldNode = previous.Find(change.node);
      const auto *newNode = current.Find(change.node);
      if (!provider || oldNode == nullptr || newNode == nullptr) {
        continue;
      }
      VARIANT oldValue;
      VARIANT newValue;
      PROPERTYID property = UIA_ItemStatusPropertyId;
      if (HasAccessibilityProperty(change.properties,
                                   AccessibilityPropertyFlags::Name)) {
        property = UIA_NamePropertyId;
        SetString(&oldValue, oldNode->label);
        SetString(&newValue, newNode->label);
      } else if (HasAccessibilityProperty(
                     change.properties,
                     AccessibilityPropertyFlags::Description)) {
        property = UIA_HelpTextPropertyId;
        SetString(&oldValue, oldNode->description);
        SetString(&newValue, newNode->description);
      } else if (HasAccessibilityProperty(change.properties,
                                          AccessibilityPropertyFlags::Value)) {
        property = UIA_ValueValuePropertyId;
        SetString(&oldValue,
                  oldNode->password ? NGIN::Text::String{} : oldNode->value);
        SetString(&newValue,
                  newNode->password ? NGIN::Text::String{} : newNode->value);
      } else if (HasAccessibilityProperty(change.properties,
                                          AccessibilityPropertyFlags::Range) &&
                 oldNode->range && newNode->range) {
        property = UIA_RangeValueValuePropertyId;
        SetDouble(&oldValue, oldNode->range->current);
        SetDouble(&newValue, newNode->range->current);
      } else if (HasAccessibilityProperty(change.properties,
                                          AccessibilityPropertyFlags::State)) {
        if (newNode->role == SemanticRole::CheckBox ||
            newNode->role == SemanticRole::Switch) {
          property = UIA_ToggleToggleStatePropertyId;
          const auto toggleState = [](const SemanticNode &node) {
            return HasSemanticState(node.states,
                                    SemanticStateFlags::Indeterminate)
                       ? static_cast<int>(ToggleState_Indeterminate)
                   : HasSemanticState(node.states, SemanticStateFlags::Checked)
                       ? static_cast<int>(ToggleState_On)
                       : static_cast<int>(ToggleState_Off);
          };
          SetInt(&oldValue, toggleState(*oldNode));
          SetInt(&newValue, toggleState(*newNode));
        } else if (newNode->role == SemanticRole::RadioButton ||
                   newNode->role == SemanticRole::ListItem ||
                   newNode->role == SemanticRole::Tab) {
          property = UIA_SelectionItemIsSelectedPropertyId;
          SetBool(&oldValue, HasSemanticState(oldNode->states,
                                              SemanticStateFlags::Selected));
          SetBool(&newValue, HasSemanticState(newNode->states,
                                              SemanticStateFlags::Selected));
        } else if (newNode->role == SemanticRole::ComboBox) {
          property = UIA_ExpandCollapseExpandCollapseStatePropertyId;
          SetInt(&oldValue,
                 HasSemanticState(oldNode->states, SemanticStateFlags::Expanded)
                     ? ExpandCollapseState_Expanded
                     : ExpandCollapseState_Collapsed);
          SetInt(&newValue,
                 HasSemanticState(newNode->states, SemanticStateFlags::Expanded)
                     ? ExpandCollapseState_Expanded
                     : ExpandCollapseState_Collapsed);
        } else {
          const auto oldDisabled =
              HasSemanticState(oldNode->states, SemanticStateFlags::Disabled);
          const auto newDisabled =
              HasSemanticState(newNode->states, SemanticStateFlags::Disabled);
          if (oldDisabled != newDisabled) {
            property = UIA_IsEnabledPropertyId;
            SetBool(&oldValue, !oldDisabled);
            SetBool(&newValue, !newDisabled);
          } else {
            property = UIA_HasKeyboardFocusPropertyId;
            SetBool(&oldValue, HasSemanticState(oldNode->states,
                                                SemanticStateFlags::Focused));
            SetBool(&newValue, HasSemanticState(newNode->states,
                                                SemanticStateFlags::Focused));
          }
        }
      } else if (HasAccessibilityProperty(change.properties,
                                          AccessibilityPropertyFlags::Role)) {
        property = UIA_ControlTypePropertyId;
        SetInt(&oldValue, ControlType(oldNode->role));
        SetInt(&newValue, ControlType(newNode->role));
      } else {
        VariantInit(&oldValue);
        VariantInit(&newValue);
      }
      CountEvent(UiaRaiseAutomationPropertyChangedEvent(
          provider.Get(), property, oldValue, newValue));
      VariantClear(&oldValue);
      VariantClear(&newValue);
    }
    for (const auto id : diff.selectionChanged) {
      auto provider = Make<ElementProvider>(state, id);
      if (provider) {
        const auto *node = current.Find(id);
        const auto event =
            node != nullptr &&
                    HasSemanticState(node->states, SemanticStateFlags::Selected)
                ? UIA_SelectionItem_ElementSelectedEventId
                : UIA_SelectionItem_ElementRemovedFromSelectionEventId;
        CountEvent(UiaRaiseAutomationEvent(provider.Get(), event));
      }
    }
    for (const auto id : diff.liveRegionChanged) {
      auto provider = Make<ElementProvider>(state, id);
      if (provider) {
        CountEvent(UiaRaiseAutomationEvent(provider.Get(),
                                           UIA_LiveRegionChangedEventId));
      }
    }
  }

  mutable std::mutex m_mutex{};
  IAccessibilityActionSink *m_sink{};
  std::unordered_map<PlatformWindowHandle, std::shared_ptr<WindowState>,
                     WindowHandleHash>
      m_windows{};
  AccessibilityDiagnostics m_diagnostics{};
};
} // namespace

auto CreateAccessibilityBackend() noexcept
    -> std::unique_ptr<IAccessibilityBackend> {
  try {
    return std::make_unique<WindowsAccessibilityBackend>();
  } catch (...) {
    return {};
  }
}
} // namespace NGIN::UI::Accessibility::Windows
