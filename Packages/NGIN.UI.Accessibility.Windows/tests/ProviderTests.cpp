#include <NGIN/UI/Accessibility/Windows/Windows.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Unknwn.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <iostream>
#include <string_view>

namespace {
using namespace NGIN::UI;

class ActionSink final : public IAccessibilityActionSink {
public:
  auto PostAccessibilityAction(AccessibilityActionRequest request) noexcept
      -> UIResult<void> override {
    last = std::move(request);
    return {};
  }
  AccessibilityActionRequest last{};
};

auto Check(const bool condition, const char *message) -> bool {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  auto provider = Accessibility::Windows::CreateAccessibilityBackend();
  ActionSink sink;
  if (!Check(provider != nullptr, "provider factory failed") ||
      !Check(provider->Initialize(sink).HasValue(), "provider init failed")) {
    return 1;
  }

  auto *window = CreateWindowExW(0, L"STATIC", L"NGIN accessibility test",
                                 WS_OVERLAPPED, 0, 0, 320, 200, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!Check(window != nullptr, "test window creation failed")) {
    return 1;
  }
  const PlatformWindowHandle handle{7, 1};
  auto attached = provider->AttachWindow(AccessibilityWindowInfo{
      .window = handle,
      .nativeWindow =
          NativeWindowInfo{
              .kind = NativeWindowKind::Win32,
              .value = reinterpret_cast<NGIN::UIntPtr>(window),
          },
      .title = NGIN::Text::String{"Provider test"},
  });
  if (!Check(attached.HasValue(), "provider attachment failed")) {
    DestroyWindow(window);
    return 1;
  }

  SemanticNode root{
      .id = SemanticNodeId{1},
      .role = SemanticRole::Window,
      .label = NGIN::Text::String{"Provider test"},
      .bounds = Rect{0.0F, 0.0F, 320.0F, 200.0F},
      .children = {SemanticNodeId{2}},
  };
  SemanticNode button{
      .id = SemanticNodeId{2},
      .parent = SemanticNodeId{1},
      .role = SemanticRole::Button,
      .label = NGIN::Text::String{"Run"},
      .bounds = Rect{10.0F, 10.0F, 100.0F, 30.0F},
      .actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus,
  };
  if (!Check(provider
                 ->Publish(AccessibilitySnapshot{
                     .window = handle,
                     .revision = 1,
                     .root = SemanticNodeId{1},
                     .nodes = {root, button},
                 })
                 .HasValue(),
             "snapshot publish failed")) {
    DestroyWindow(window);
    return 1;
  }

  const auto object = SendMessageW(window, WM_GETOBJECT, 0, UiaRootObjectId);
  Microsoft::WRL::ComPtr<IUIAutomation> automation;
  Microsoft::WRL::ComPtr<IUIAutomationElement> element;
  const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const auto automationCreated =
      CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&automation));
  const auto elementCreated =
      SUCCEEDED(automationCreated)
          ? automation->ElementFromHandle(window, &element)
          : automationCreated;
  BSTR name{};
  const auto nameRead = SUCCEEDED(elementCreated)
                            ? element->get_CurrentName(&name)
                            : elementCreated;
  VARIANT buttonType;
  VariantInit(&buttonType);
  buttonType.vt = VT_I4;
  buttonType.lVal = UIA_ButtonControlTypeId;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> buttonCondition;
  Microsoft::WRL::ComPtr<IUIAutomationElement> buttonElement;
  Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invoke;
  const auto conditionCreated =
      SUCCEEDED(elementCreated)
          ? automation->CreatePropertyCondition(UIA_ControlTypePropertyId,
                                                buttonType, &buttonCondition)
          : elementCreated;
  const auto buttonFound =
      SUCCEEDED(conditionCreated)
          ? element->FindFirst(TreeScope_Descendants, buttonCondition.Get(),
                               &buttonElement)
          : conditionCreated;
  BSTR buttonName{};
  const auto buttonNameRead = SUCCEEDED(buttonFound) && buttonElement
                                  ? buttonElement->get_CurrentName(&buttonName)
                                  : E_FAIL;
  const auto patternRead = SUCCEEDED(buttonNameRead)
                               ? buttonElement->GetCurrentPatternAs(
                                     UIA_InvokePatternId, IID_PPV_ARGS(&invoke))
                               : buttonNameRead;
  const auto invoked =
      SUCCEEDED(patternRead) && invoke ? invoke->Invoke() : E_FAIL;
  button.label = NGIN::Text::String{"Run now"};
  button.states = SemanticStateFlags::Focused | SemanticStateFlags::Selected;
  button.live = SemanticLiveSetting::Polite;
  const auto updated = provider->Publish(AccessibilitySnapshot{
      .window = handle,
      .revision = 2,
      .root = SemanticNodeId{1},
      .focused = SemanticNodeId{2},
      .nodes = {root, button},
  });
  const auto diagnostics = provider->Diagnostics();
  const auto detached = provider->DetachWindow(handle);
  BSTR staleName{};
  const auto staleRead =
      element ? element->get_CurrentName(&staleName) : E_UNEXPECTED;
  const bool passed =
      Check(object != 0, "WM_GETOBJECT did not expose a provider") &&
      Check(SUCCEEDED(nameRead), "UI Automation could not read the root") &&
      Check(name != nullptr && std::wstring_view{name} == L"Provider test",
            "UI Automation returned the wrong root name") &&
      Check(SUCCEEDED(invoked), "UI Automation could not invoke the button") &&
      Check(buttonName != nullptr && std::wstring_view{buttonName} == L"Run",
            "UI Automation returned the wrong button name") &&
      Check(sink.last.semantic.node == SemanticNodeId{2} &&
                sink.last.semantic.action == SemanticActionKind::Activate,
            "UI Automation did not post the button action") &&
      Check(updated.HasValue(), "updated snapshot publish failed") &&
      Check(diagnostics.available, "provider did not report availability") &&
      Check(diagnostics.attachedWindowCount == 1,
            "provider did not track its window") &&
      Check(diagnostics.publishedSnapshotCount == 2,
            "provider did not track its snapshot") &&
      Check(diagnostics.raisedEventCount > 0,
            "provider did not raise snapshot events") &&
      Check(detached.HasValue(), "provider detach failed") &&
      Check(provider->Diagnostics().attachedWindowCount == 0,
            "provider retained a detached window") &&
      Check(FAILED(staleRead), "detached element remained available");
  SysFreeString(name);
  SysFreeString(buttonName);
  SysFreeString(staleName);
  element.Reset();
  automation.Reset();
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
  DestroyWindow(window);
  return passed ? 0 : 1;
}
