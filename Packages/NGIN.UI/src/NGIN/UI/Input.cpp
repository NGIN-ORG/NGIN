#include <NGIN/UI/Input.hpp>

#include "ScrollBarGeometry.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <string_view>
#include <type_traits>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto HasCommandModifier(const UInt32 modifiers) noexcept -> bool {
  return HasKeyModifier(modifiers, KeyModifierFlags::Control) ||
         HasKeyModifier(modifiers, KeyModifierFlags::Super);
}

[[nodiscard]] auto IsLogicalCharacter(const LogicalKey key,
                                      const char character) noexcept -> bool {
  const auto value = static_cast<UInt32>(key);
  return value == static_cast<UInt32>(character) ||
         value == static_cast<UInt32>(character - 'A' + 'a');
}

[[nodiscard]] auto IsScrollable(const ElementType type) noexcept -> bool {
  return type == ElementType::ScrollView || type == ElementType::ListView;
}

[[nodiscard]] auto IsActivatable(const ElementType type) noexcept -> bool {
  return type == ElementType::Button || type == ElementType::ListItem ||
         type == ElementType::Tab || type == ElementType::MenuItem;
}

[[nodiscard]] auto IsPrintableKey(const LogicalKey key) noexcept -> bool {
  const auto value = static_cast<UInt32>(key);
  return value >= 32U && value <= 126U;
}

[[nodiscard]] auto CustomContextFor(RuntimeNode &node, const F32 scaleFactor)
    -> CustomElementContext {
  return CustomElementContext{
      *node.custom.state,
      node.id,
      node.arrangedBounds,
      CustomInteractionState{
          .hovered = node.interaction.hovered,
          .pressed =
              node.interaction.pressed || node.interaction.keyboardPressed,
          .focused = node.interaction.focused,
          .enabled = node.properties.interaction.enabled,
      },
      scaleFactor,
  };
}

void ReportCustomError(const RuntimeNode &node, const UIError &error) noexcept {
  if (!node.properties.custom.onError) {
    return;
  }
  try {
    node.properties.custom.onError(error);
  } catch (...) {
  }
}
} // namespace

InputRouter::InputRouter(RuntimeTree &tree, IPlatformBackend *platform,
                         const PlatformWindowHandle window,
                         const F32 scaleFactor) noexcept
    : m_tree(tree), m_platform(platform), m_window(window),
      m_scaleFactor(scaleFactor > 0.0F ? scaleFactor : 1.0F) {}

auto InputRouter::HitTest(const Point position) const noexcept
    -> ElementHandle {
  for (auto popup = m_popups.rbegin(); popup != m_popups.rend(); ++popup) {
    if (const auto hit =
            HitTestSubtree(popup->handle, position, popup->handle)) {
      return hit;
    }
  }
  if (TopModalPopup()) {
    return {};
  }
  return HitTestSubtree(m_tree.Root(), position);
}

auto InputRouter::FocusedElement() const noexcept -> ElementHandle {
  return m_tree.IsAlive(m_focused) ? m_focused : ElementHandle{};
}

auto InputRouter::CapturedElement(const UInt64 pointerId) const noexcept
    -> ElementHandle {
  const auto found = m_captured.find(pointerId);
  return found != m_captured.end() && m_tree.IsAlive(found->second)
             ? found->second
             : ElementHandle{};
}

auto InputRouter::HoveredElement(const UInt64 pointerId) const noexcept
    -> ElementHandle {
  const auto found = m_hovered.find(pointerId);
  return found != m_hovered.end() && m_tree.IsAlive(found->second)
             ? found->second
             : ElementHandle{};
}

auto InputRouter::FirstCapturedElement() const noexcept -> ElementHandle {
  for (const auto &[pointerId, handle] : m_captured) {
    static_cast<void>(pointerId);
    if (m_tree.IsAlive(handle)) {
      return handle;
    }
  }
  return {};
}

auto InputRouter::Route(const PlatformEvent &event) -> InputDispatchResult {
  m_customInvalidation = InvalidationKind::None;
  auto result = std::visit(
      [this](const auto &value) -> InputDispatchResult {
        using Event = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, PointerMoved>) {
          return RouteMoved(value);
        } else if constexpr (std::is_same_v<Event, PointerButtonChanged>) {
          return RouteButton(value);
        } else if constexpr (std::is_same_v<Event, PointerWheelChanged>) {
          return RouteWheel(value);
        } else if constexpr (std::is_same_v<Event, KeyChanged>) {
          return RouteKey(value);
        } else if constexpr (std::is_same_v<Event, TextInput>) {
          return RouteText(value);
        } else if constexpr (std::is_same_v<Event, TextComposition>) {
          return RouteText(value);
        } else if constexpr (std::is_same_v<Event, WindowFocusChanged>) {
          if (!value.focused) {
            return InputDispatchResult{
                .visualStateChanged = ClearFocus(),
            };
          }
        }
        return {};
      },
      event);
  result.invalidation |= m_customInvalidation;
  return result;
}

auto InputRouter::SetFocus(const ElementHandle handle) noexcept -> bool {
  if (handle == m_focused) {
    return false;
  }

  const auto modalPopup = TopModalPopup();
  if (modalPopup && !IsWithin(handle, modalPopup)) {
    return false;
  }
  auto *next = m_tree.Get(handle);
  if (next == nullptr || !next->properties.interaction.enabled ||
      !next->properties.interaction.focusable ||
      !IsEffectivelyVisible(handle)) {
    return false;
  }

  if (auto *previous = m_tree.Get(m_focused); previous != nullptr) {
    StopTextInput(*previous);
    previous->interaction.focused = false;
    if (previous->interaction.keyboardPressed) {
      previous->interaction.keyboardPressed = false;
      previous->interaction.pressed = false;
    }
  }
  m_focused = handle;
  next->interaction.focused = true;
  StartTextInput(*next);
  return true;
}

auto InputRouter::ClearFocus() noexcept -> bool {
  auto *focused = m_tree.Get(m_focused);
  if (focused == nullptr) {
    StopOrphanedTextInput();
    m_focused = {};
    return false;
  }
  StopTextInput(*focused);
  focused->interaction.focused = false;
  if (focused->interaction.keyboardPressed) {
    focused->interaction.keyboardPressed = false;
    focused->interaction.pressed = false;
  }
  m_focused = {};
  return true;
}

void InputRouter::SetScaleFactor(const F32 scaleFactor) noexcept {
  if (scaleFactor <= 0.0F) {
    return;
  }
  m_scaleFactor = scaleFactor;
  if (const auto *focused = m_tree.Get(FocusedElement());
      focused != nullptr && focused->type == ElementType::TextField) {
    StartTextInput(*focused);
  }
}

auto InputRouter::MoveFocus(const bool reverse) -> bool {
  const auto candidates = FocusCandidates(TopModalPopup());
  if (candidates.empty()) {
    return ClearFocus();
  }

  const auto current =
      std::find(candidates.begin(), candidates.end(), FocusedElement());
  if (current == candidates.end()) {
    return SetFocus(reverse ? candidates.back() : candidates.front());
  }

  const auto index =
      static_cast<UIntSize>(std::distance(candidates.begin(), current));
  const auto nextIndex = reverse
                             ? (index == 0 ? candidates.size() - 1 : index - 1)
                             : (index + 1) % candidates.size();
  return SetFocus(candidates[nextIndex]);
}

void InputRouter::Synchronize() noexcept {
  std::vector<ElementHandle> discoveredPopups;
  CollectPopups(m_tree.Root(), discoveredPopups);

  ElementHandle restoreFocus{};
  const auto focusNeedsRestoration =
      !m_tree.IsAlive(m_focused) || !IsEffectivelyVisible(m_focused);
  if (focusNeedsRestoration && m_tree.IsAlive(m_focused)) {
    static_cast<void>(ClearFocus());
  }
  for (auto session = m_popups.rbegin(); session != m_popups.rend();
       ++session) {
    if (std::find(discoveredPopups.begin(), discoveredPopups.end(),
                  session->handle) == discoveredPopups.end() &&
        !restoreFocus && m_tree.IsAlive(session->restoreFocus)) {
      restoreFocus = session->restoreFocus;
    }
  }

  std::vector<PopupSession> nextPopups;
  nextPopups.reserve(discoveredPopups.size());
  bool addedPopup = false;
  for (const auto popup : discoveredPopups) {
    const auto retained = std::find_if(m_popups.begin(), m_popups.end(),
                                       [popup](const PopupSession &session) {
                                         return session.handle == popup;
                                       });
    if (retained != m_popups.end()) {
      nextPopups.push_back(*retained);
    } else {
      addedPopup = true;
      nextPopups.push_back(PopupSession{
          .handle = popup,
          .restoreFocus = FocusedElement(),
      });
    }
  }
  m_popups = std::move(nextPopups);

  const auto *focused = m_tree.Get(m_focused);
  if (focused == nullptr) {
    StopOrphanedTextInput();
    m_focused = {};
  } else if (!focused->properties.interaction.enabled ||
             !focused->properties.interaction.focusable) {
    static_cast<void>(ClearFocus());
  }

  std::erase_if(m_captured, [this](const auto &entry) {
    auto *node = m_tree.Get(entry.second);
    if (node == nullptr) {
      return true;
    }
    if (!node->properties.interaction.enabled ||
        !IsEffectivelyVisible(entry.second)) {
      node->interaction.pressed = false;
      return true;
    }
    if (const auto modalPopup = TopModalPopup();
        modalPopup && !IsWithin(entry.second, modalPopup)) {
      node->interaction.pressed = false;
      return true;
    }
    return false;
  });
  std::erase_if(m_hovered, [this](const auto &entry) {
    auto *node = m_tree.Get(entry.second);
    if (node == nullptr) {
      return true;
    }
    if (!node->properties.interaction.hitTestVisible ||
        !IsEffectivelyVisible(entry.second)) {
      node->interaction.hovered = false;
      return true;
    }
    if (const auto modalPopup = TopModalPopup();
        modalPopup && !IsWithin(entry.second, modalPopup)) {
      node->interaction.hovered = false;
      return true;
    }
    return false;
  });

  if (focusNeedsRestoration && restoreFocus) {
    static_cast<void>(SetFocus(restoreFocus));
  }
  if (addedPopup && !m_popups.empty()) {
    const auto popup = m_popups.back().handle;
    const auto candidates = FocusCandidates(popup);
    if (!candidates.empty()) {
      static_cast<void>(SetFocus(candidates.front()));
    } else if (const auto *node = m_tree.Get(popup);
               node != nullptr && node->properties.popup.modal) {
      static_cast<void>(ClearFocus());
    }
  } else if (const auto modalPopup = TopModalPopup();
             modalPopup && !IsWithin(FocusedElement(), modalPopup)) {
    const auto candidates = FocusCandidates(modalPopup);
    if (!candidates.empty()) {
      static_cast<void>(SetFocus(candidates.front()));
    } else {
      static_cast<void>(ClearFocus());
    }
  }
}

auto InputRouter::HitTestSubtree(const ElementHandle handle,
                                 const Point position,
                                 const ElementHandle popupRoot) const noexcept
    -> ElementHandle {
  const auto *node = m_tree.Get(handle);
  if (node == nullptr || !node->properties.interaction.hitTestVisible ||
      node->properties.visibility != ElementVisibility::Visible ||
      (node->type == ElementType::Popup && handle != popupRoot) ||
      !node->arrangedBounds.Contains(position)) {
    return {};
  }

  for (auto child = node->children.rbegin(); child != node->children.rend();
       ++child) {
    if (const auto hit = HitTestSubtree(*child, position, popupRoot); hit) {
      return hit;
    }
  }
  return handle == m_tree.Root() || node->type == ElementType::Popup
             ? ElementHandle{}
             : handle;
}

void InputRouter::CollectPopups(const ElementHandle handle,
                                std::vector<ElementHandle> &popups) const {
  const auto *node = m_tree.Get(handle);
  if (node == nullptr ||
      node->properties.visibility != ElementVisibility::Visible) {
    return;
  }
  if (node->type == ElementType::Popup) {
    popups.push_back(handle);
  }
  for (const auto child : node->children) {
    CollectPopups(child, popups);
  }
}

auto InputRouter::IsWithin(const ElementHandle handle,
                           const ElementHandle ancestor) const noexcept
    -> bool {
  auto current = handle;
  while (const auto *node = m_tree.Get(current)) {
    if (current == ancestor) {
      return true;
    }
    current = node->parent;
  }
  return false;
}

auto InputRouter::IsEffectivelyVisible(ElementHandle handle) const noexcept
    -> bool {
  while (const auto *node = m_tree.Get(handle)) {
    if (node->properties.visibility != ElementVisibility::Visible) {
      return false;
    }
    handle = node->parent;
  }
  return true;
}

auto InputRouter::ActivatableAncestor(ElementHandle handle) const noexcept
    -> ElementHandle {
  while (const auto *node = m_tree.Get(handle)) {
    if (IsActivatable(node->type) && node->properties.interaction.enabled) {
      return handle;
    }
    if (node->type == ElementType::Popup || handle == m_tree.Root()) {
      break;
    }
    handle = node->parent;
  }
  return {};
}

auto InputRouter::FocusableAncestor(ElementHandle handle) const noexcept
    -> ElementHandle {
  while (const auto *node = m_tree.Get(handle)) {
    if (node->properties.interaction.enabled &&
        node->properties.interaction.focusable &&
        IsEffectivelyVisible(handle)) {
      return handle;
    }
    handle = node->parent;
  }
  return {};
}

auto InputRouter::TopPopup() const noexcept -> ElementHandle {
  for (auto popup = m_popups.rbegin(); popup != m_popups.rend(); ++popup) {
    if (m_tree.IsAlive(popup->handle)) {
      return popup->handle;
    }
  }
  return {};
}

auto InputRouter::TopModalPopup() const noexcept -> ElementHandle {
  for (auto popup = m_popups.rbegin(); popup != m_popups.rend(); ++popup) {
    const auto *node = m_tree.Get(popup->handle);
    if (node != nullptr && node->properties.popup.modal) {
      return popup->handle;
    }
  }
  return {};
}

auto InputRouter::BuildPath(const ElementHandle target) const
    -> std::vector<ElementHandle> {
  std::vector<ElementHandle> path;
  auto current = target;
  while (const auto *node = m_tree.Get(current)) {
    path.push_back(current);
    current = node->parent;
  }
  return path;
}

auto InputRouter::Dispatch(RoutedPointerEvent &event,
                           const ElementHandle target) -> DispatchOutcome {
  DispatchOutcome outcome{};
  if (!m_tree.IsAlive(target)) {
    return outcome;
  }

  event.target = target;
  const auto path = BuildPath(target);
  for (auto index = path.size(); index > 1 && !event.handled; --index) {
    InvokeHandler(path[index - 1], EventPhase::Capture, event, outcome);
  }
  if (!event.handled) {
    InvokeHandler(target, EventPhase::Target, event, outcome);
  }
  for (UIntSize index = 1; index < path.size() && !event.handled; ++index) {
    InvokeHandler(path[index], EventPhase::Bubble, event, outcome);
  }
  outcome.handled = event.handled;
  return outcome;
}

void InputRouter::InvokeHandler(const ElementHandle handle,
                                const EventPhase phase,
                                RoutedPointerEvent &event,
                                DispatchOutcome &outcome) const {
  auto *node = m_tree.Get(handle);
  if (node == nullptr ||
      (!node->properties.interaction.onPointer &&
       (node->type != ElementType::CustomElement || !node->custom.state ||
        !node->properties.custom.element))) {
    return;
  }

  event.phase = phase;
  event.currentTarget = handle;
  event.captureRequested = false;
  event.captureReleaseRequested = false;
  if (node->properties.interaction.onPointer) {
    node->properties.interaction.onPointer(event);
    outcome.callbackInvoked = true;
  }
  if (node->type == ElementType::CustomElement && node->custom.state &&
      node->properties.custom.element) {
    try {
      auto context = CustomContextFor(*node, m_scaleFactor);
      auto handled =
          node->properties.custom.element->PointerEvent(context, event);
      if (handled) {
        m_customInvalidation |= handled.Value();
        outcome.callbackInvoked = outcome.callbackInvoked ||
                                  handled.Value() != InvalidationKind::None ||
                                  event.handled;
      } else {
        ReportCustomError(*node, handled.Error());
        m_customInvalidation |=
            InvalidationKind::Paint | InvalidationKind::Semantics;
        outcome.callbackInvoked = true;
      }
    } catch (const std::bad_alloc &) {
      ReportCustomError(*node,
                        MakeUIError(UIErrorCode::OutOfMemory,
                                    "Custom pointer callback allocation failed",
                                    "NGIN.UI", "ICustomElement::PointerEvent"));
      m_customInvalidation |=
          InvalidationKind::Paint | InvalidationKind::Semantics;
      outcome.callbackInvoked = true;
    } catch (...) {
      ReportCustomError(
          *node, MakeUIError(UIErrorCode::InvalidState,
                             "Custom pointer callback threw an exception",
                             "NGIN.UI", "ICustomElement::PointerEvent"));
      m_customInvalidation |=
          InvalidationKind::Paint | InvalidationKind::Semantics;
      outcome.callbackInvoked = true;
    }
    m_tree.SynchronizeCustom(*node, m_scaleFactor);
  }
  if (event.captureRequested) {
    outcome.captureRequest = handle;
  }
  outcome.releaseCapture =
      outcome.releaseCapture || event.captureReleaseRequested;
}

auto InputRouter::Dispatch(RoutedKeyEvent &event, const ElementHandle target)
    -> InputDispatchResult {
  InputDispatchResult result{};
  if (!m_tree.IsAlive(target)) {
    return result;
  }

  event.target = target;
  const auto path = BuildPath(target);
  const auto invoke = [this, &event, &result](const ElementHandle handle,
                                              const EventPhase phase) {
    auto *node = m_tree.Get(handle);
    if (node == nullptr ||
        (!node->properties.interaction.onKey &&
         (node->type != ElementType::CustomElement || !node->custom.state ||
          !node->properties.custom.element))) {
      return;
    }
    event.phase = phase;
    event.currentTarget = handle;
    if (node->properties.interaction.onKey) {
      node->properties.interaction.onKey(event);
      result.callbackInvoked = true;
    }
    if (node->type == ElementType::CustomElement && node->custom.state &&
        node->properties.custom.element) {
      try {
        auto context = CustomContextFor(*node, m_scaleFactor);
        auto handled =
            node->properties.custom.element->KeyEvent(context, event);
        if (handled) {
          m_customInvalidation |= handled.Value();
          result.callbackInvoked = result.callbackInvoked ||
                                   handled.Value() != InvalidationKind::None ||
                                   event.handled;
        } else {
          ReportCustomError(*node, handled.Error());
          m_customInvalidation |=
              InvalidationKind::Paint | InvalidationKind::Semantics;
          result.callbackInvoked = true;
        }
      } catch (...) {
        ReportCustomError(*node,
                          MakeUIError(UIErrorCode::InvalidState,
                                      "Custom key callback threw an exception",
                                      "NGIN.UI", "ICustomElement::KeyEvent"));
        m_customInvalidation |=
            InvalidationKind::Paint | InvalidationKind::Semantics;
        result.callbackInvoked = true;
      }
      m_tree.SynchronizeCustom(*node, m_scaleFactor);
    }
  };

  for (auto index = path.size(); index > 1 && !event.handled; --index) {
    invoke(path[index - 1], EventPhase::Capture);
  }
  if (!event.handled) {
    invoke(target, EventPhase::Target);
  }
  for (UIntSize index = 1; index < path.size() && !event.handled; ++index) {
    invoke(path[index], EventPhase::Bubble);
  }
  result.handled = event.handled;
  return result;
}

auto InputRouter::Dispatch(RoutedTextEvent &event, const ElementHandle target)
    -> InputDispatchResult {
  InputDispatchResult result{};
  if (!m_tree.IsAlive(target)) {
    return result;
  }

  event.target = target;
  const auto path = BuildPath(target);
  const auto invoke = [this, &event, &result](const ElementHandle handle,
                                              const EventPhase phase) {
    auto *node = m_tree.Get(handle);
    if (node == nullptr ||
        (!node->properties.interaction.onText &&
         (node->type != ElementType::CustomElement || !node->custom.state ||
          !node->properties.custom.element))) {
      return;
    }
    event.phase = phase;
    event.currentTarget = handle;
    if (node->properties.interaction.onText) {
      node->properties.interaction.onText(event);
      result.callbackInvoked = true;
    }
    if (node->type == ElementType::CustomElement && node->custom.state &&
        node->properties.custom.element) {
      try {
        auto context = CustomContextFor(*node, m_scaleFactor);
        auto handled =
            node->properties.custom.element->TextEvent(context, event);
        if (handled) {
          m_customInvalidation |= handled.Value();
          result.callbackInvoked = result.callbackInvoked ||
                                   handled.Value() != InvalidationKind::None ||
                                   event.handled;
        } else {
          ReportCustomError(*node, handled.Error());
          m_customInvalidation |=
              InvalidationKind::Paint | InvalidationKind::Semantics;
          result.callbackInvoked = true;
        }
      } catch (...) {
        ReportCustomError(*node,
                          MakeUIError(UIErrorCode::InvalidState,
                                      "Custom text callback threw an exception",
                                      "NGIN.UI", "ICustomElement::TextEvent"));
        m_customInvalidation |=
            InvalidationKind::Paint | InvalidationKind::Semantics;
        result.callbackInvoked = true;
      }
      m_tree.SynchronizeCustom(*node, m_scaleFactor);
    }
  };

  for (auto index = path.size(); index > 1 && !event.handled; --index) {
    invoke(path[index - 1], EventPhase::Capture);
  }
  if (!event.handled) {
    invoke(target, EventPhase::Target);
  }
  for (UIntSize index = 1; index < path.size() && !event.handled; ++index) {
    invoke(path[index], EventPhase::Bubble);
  }
  result.handled = event.handled;
  return result;
}

auto InputRouter::FocusCandidates(const ElementHandle scope) const
    -> std::vector<ElementHandle> {
  struct Candidate final {
    ElementHandle handle{};
    Int32 tabIndex{0};
  };

  std::vector<Candidate> candidates;
  std::vector<ElementHandle> pending{scope ? scope : m_tree.Root()};
  while (!pending.empty()) {
    const auto handle = pending.back();
    pending.pop_back();
    const auto *node = m_tree.Get(handle);
    if (node == nullptr ||
        node->properties.visibility != ElementVisibility::Visible) {
      continue;
    }
    const auto &interaction = node->properties.interaction;
    if (handle != m_tree.Root() && interaction.enabled &&
        interaction.focusable && interaction.tabIndex >= 0) {
      candidates.push_back(Candidate{
          .handle = handle,
          .tabIndex = interaction.tabIndex,
      });
    }
    for (auto child = node->children.rbegin(); child != node->children.rend();
         ++child) {
      pending.push_back(*child);
    }
  }

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate &left, const Candidate &right) {
                     const auto leftOrder =
                         left.tabIndex == 0 ? std::numeric_limits<Int32>::max()
                                            : left.tabIndex;
                     const auto rightOrder =
                         right.tabIndex == 0 ? std::numeric_limits<Int32>::max()
                                             : right.tabIndex;
                     return leftOrder < rightOrder;
                   });

  std::vector<ElementHandle> result;
  result.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    result.push_back(candidate.handle);
  }
  return result;
}

void InputRouter::ReportTextFieldError(const RuntimeNode &node,
                                       const UIError &error) const {
  if (node.properties.textField.onError) {
    node.properties.textField.onError(error);
  }
}

void InputRouter::StartTextInput(const RuntimeNode &node) noexcept {
  if (node.type != ElementType::TextField || m_platform == nullptr ||
      !m_window ||
      !HasPlatformCapability(m_platform->Capabilities(),
                             PlatformCapabilityFlags::IME)) {
    return;
  }
  const auto started = m_platform->StartTextInput(
      m_window, ToPixelRect(node.arrangedBounds, m_scaleFactor));
  m_textInputActive = m_textInputActive || started.HasValue();
}

void InputRouter::StopTextInput(const RuntimeNode &node) noexcept {
  if (node.type != ElementType::TextField) {
    return;
  }
  if (node.textField.editing) {
    static_cast<void>(node.textField.editing->CancelComposition());
  }
  if (m_platform == nullptr || !m_window ||
      !HasPlatformCapability(m_platform->Capabilities(),
                             PlatformCapabilityFlags::IME)) {
    m_textInputActive = false;
    return;
  }
  static_cast<void>(m_platform->StopTextInput(m_window));
  m_textInputActive = false;
}

void InputRouter::StopOrphanedTextInput() noexcept {
  if (!m_textInputActive) {
    return;
  }
  if (m_platform != nullptr && m_window &&
      HasPlatformCapability(m_platform->Capabilities(),
                            PlatformCapabilityFlags::IME)) {
    static_cast<void>(m_platform->StopTextInput(m_window));
  }
  m_textInputActive = false;
}

auto InputRouter::CommitTextFieldEdit(
    RuntimeNode &node,
    NGIN::Utilities::Callable<UIResult<void>(TextEditingBuffer &)> edit)
    -> InputDispatchResult {
  InputDispatchResult result{.handled = true};
  if (node.properties.textField.readOnly) {
    return result;
  }
  if (!node.textField.editing ||
      !node.properties.textField.value.IsWritable()) {
    const auto error =
        MakeUIError(UIErrorCode::InvalidState,
                    "TextField does not have a writable editing session",
                    "NGIN.UI", "InputRouter::CommitTextFieldEdit");
    ReportTextFieldError(node, error);
    result.callbackInvoked =
        static_cast<bool>(node.properties.textField.onError);
    return result;
  }

  auto &editing = *node.textField.editing;
  const auto previousEditing = editing;
  auto edited = edit(editing);
  if (!edited) {
    ReportTextFieldError(node, edited.Error());
    result.callbackInvoked =
        static_cast<bool>(node.properties.textField.onError);
    return result;
  }

  auto committed = node.properties.textField.value.Set(editing.Value());
  if (!committed) {
    editing = previousEditing;
    ReportTextFieldError(node, committed.Error());
    result.callbackInvoked =
        static_cast<bool>(node.properties.textField.onError);
    return result;
  }

  result.layoutStateChanged = true;
  return result;
}

auto InputRouter::RouteTextFieldInput(RuntimeNode &node,
                                      const NGIN::Text::String &text)
    -> InputDispatchResult {
  return CommitTextFieldEdit(
      node, [text](TextEditingBuffer &editing) -> UIResult<void> {
        return editing.HasComposition() ? editing.CommitComposition(text)
                                        : editing.ReplaceSelection(text);
      });
}

auto InputRouter::RouteTextFieldComposition(RuntimeNode &node,
                                            const TextComposition &event)
    -> InputDispatchResult {
  InputDispatchResult result{.handled = true};
  if (node.properties.textField.readOnly || !node.textField.editing) {
    return result;
  }

  auto updated = node.textField.editing->UpdateComposition(
      event.text, event.selectionStart, event.selectionLength);
  if (!updated) {
    ReportTextFieldError(node, updated.Error());
    result.callbackInvoked =
        static_cast<bool>(node.properties.textField.onError);
    return result;
  }
  result.layoutStateChanged = true;
  return result;
}

auto InputRouter::RouteTextFieldKey(RuntimeNode &node, const KeyChanged &event)
    -> InputDispatchResult {
  if (!node.textField.editing ||
      (event.state != KeyState::Pressed && event.state != KeyState::Repeated)) {
    return {};
  }

  auto &editing = *node.textField.editing;
  const auto command = HasCommandModifier(event.modifiers);
  const auto extend = HasKeyModifier(event.modifiers, KeyModifierFlags::Shift);
  const auto key = event.Logical();

  if (command && IsLogicalCharacter(key, 'A')) {
    editing.SelectAll();
    return InputDispatchResult{
        .handled = true,
        .visualStateChanged = true,
    };
  }

  if (command &&
      (IsLogicalCharacter(key, 'C') || IsLogicalCharacter(key, 'X'))) {
    InputDispatchResult result{.handled = true};
    if (m_platform == nullptr ||
        !HasPlatformCapability(m_platform->Capabilities(),
                               PlatformCapabilityFlags::Clipboard)) {
      const auto error =
          MakeUIError(UIErrorCode::Unsupported,
                      "Clipboard commands require a platform backend",
                      "NGIN.UI", "InputRouter::RouteTextFieldKey");
      ReportTextFieldError(node, error);
      result.callbackInvoked =
          static_cast<bool>(node.properties.textField.onError);
      return result;
    }
    auto copied = m_platform->SetClipboardText(editing.SelectedText());
    if (!copied) {
      ReportTextFieldError(node, copied.Error());
      result.callbackInvoked =
          static_cast<bool>(node.properties.textField.onError);
      return result;
    }
    if (IsLogicalCharacter(key, 'X') && !node.properties.textField.readOnly) {
      return CommitTextFieldEdit(
          node, [](TextEditingBuffer &buffer) -> UIResult<void> {
            return buffer.ReplaceSelection({});
          });
    }
    return result;
  }

  if (command && IsLogicalCharacter(key, 'V')) {
    if (m_platform == nullptr ||
        !HasPlatformCapability(m_platform->Capabilities(),
                               PlatformCapabilityFlags::Clipboard)) {
      InputDispatchResult result{.handled = true};
      const auto error =
          MakeUIError(UIErrorCode::Unsupported,
                      "Clipboard commands require a platform backend",
                      "NGIN.UI", "InputRouter::RouteTextFieldKey");
      ReportTextFieldError(node, error);
      result.callbackInvoked =
          static_cast<bool>(node.properties.textField.onError);
      return result;
    }
    auto pasted = m_platform->GetClipboardText();
    if (!pasted) {
      InputDispatchResult result{.handled = true};
      ReportTextFieldError(node, pasted.Error());
      result.callbackInvoked =
          static_cast<bool>(node.properties.textField.onError);
      return result;
    }
    return RouteTextFieldInput(node, pasted.Value());
  }

  if (key == LogicalKey::Backspace) {
    return CommitTextFieldEdit(node,
                               [](TextEditingBuffer &buffer) -> UIResult<void> {
                                 return buffer.DeleteBackward();
                               });
  }
  if (key == LogicalKey::Delete) {
    return CommitTextFieldEdit(node,
                               [](TextEditingBuffer &buffer) -> UIResult<void> {
                                 return buffer.DeleteForward();
                               });
  }

  UIntSize target = editing.State().caretCluster;
  if (key == LogicalKey::Home) {
    target = 0;
  } else if (key == LogicalKey::End) {
    target = editing.Clusters().size();
  } else if (key == LogicalKey::Left) {
    if (!extend && !editing.State().selection.Empty()) {
      target = editing.State().selection.start;
    } else if (target > 0) {
      --target;
    }
  } else if (key == LogicalKey::Right) {
    if (!extend && !editing.State().selection.Empty()) {
      target = editing.State().selection.End();
    } else if (target < editing.Clusters().size()) {
      ++target;
    }
  } else {
    return {};
  }

  auto moved = editing.MoveCaretTo(target, extend);
  if (!moved) {
    ReportTextFieldError(node, moved.Error());
    return InputDispatchResult{
        .handled = true,
        .callbackInvoked = static_cast<bool>(node.properties.textField.onError),
    };
  }
  return InputDispatchResult{
      .handled = true,
      .visualStateChanged = true,
  };
}

void InputRouter::CollectListItems(const ElementHandle handle,
                                   const ElementHandle listRoot,
                                   std::vector<ElementHandle> &items) const {
  const auto *node = m_tree.Get(handle);
  if (node == nullptr ||
      node->properties.visibility != ElementVisibility::Visible) {
    return;
  }
  if (handle != listRoot && node->type == ElementType::ListView) {
    return;
  }
  if (node->type == ElementType::ListItem &&
      node->properties.interaction.enabled) {
    items.push_back(handle);
    return;
  }
  for (const auto child : node->children) {
    CollectListItems(child, listRoot, items);
  }
}

auto InputRouter::ActivateListItem(const ElementHandle listHandle,
                                   const ElementHandle itemHandle)
    -> InputDispatchResult {
  auto *list = m_tree.Get(listHandle);
  auto *item = m_tree.Get(itemHandle);
  if (list == nullptr || item == nullptr ||
      !item->properties.interaction.enabled) {
    return {};
  }

  InputDispatchResult result{
      .handled = true,
      .visualStateChanged = true,
      .activated = true,
  };
  if (item->properties.interaction.onActivate) {
    item->properties.interaction.onActivate();
    result.callbackInvoked = true;
  }

  const auto viewport = list->arrangedBounds;
  const auto itemBounds = item->arrangedBounds;
  const auto previous = list->scroll.offset;
  if (list->properties.scroll.vertical) {
    if (itemBounds.y < viewport.y) {
      list->scroll.offset.y -= viewport.y - itemBounds.y;
    } else if (itemBounds.y + itemBounds.height >
               viewport.y + viewport.height) {
      list->scroll.offset.y +=
          itemBounds.y + itemBounds.height - viewport.y - viewport.height;
    }
    list->scroll.offset.y =
        std::clamp(list->scroll.offset.y, 0.0F,
                   std::max(0.0F, list->scroll.contentSize.height -
                                      list->scroll.viewportSize.height));
  }
  if (list->properties.scroll.horizontal) {
    if (itemBounds.x < viewport.x) {
      list->scroll.offset.x -= viewport.x - itemBounds.x;
    } else if (itemBounds.x + itemBounds.width > viewport.x + viewport.width) {
      list->scroll.offset.x +=
          itemBounds.x + itemBounds.width - viewport.x - viewport.width;
    }
    list->scroll.offset.x =
        std::clamp(list->scroll.offset.x, 0.0F,
                   std::max(0.0F, list->scroll.contentSize.width -
                                      list->scroll.viewportSize.width));
  }
  result.layoutStateChanged = list->scroll.offset != previous;
  return result;
}

auto InputRouter::RouteListKey(const ElementHandle listHandle,
                               const KeyChanged &event) -> InputDispatchResult {
  if (event.state == KeyState::Released) {
    return {};
  }
  if (m_typeAheadList != listHandle) {
    m_typeAheadList = listHandle;
    m_typeAheadPrefix.clear();
    m_typeAheadTime = {};
  }
  std::vector<ElementHandle> items;
  CollectListItems(listHandle, listHandle, items);
  if (items.empty()) {
    return {};
  }

  auto selected = items.end();
  for (auto item = items.begin(); item != items.end(); ++item) {
    const auto *node = m_tree.Get(*item);
    if (node != nullptr && HasSemanticState(node->properties.semantics.states,
                                            SemanticStateFlags::Selected)) {
      selected = item;
      break;
    }
  }

  const auto key = event.Logical();
  if (key == LogicalKey::Up || key == LogicalKey::Left ||
      key == LogicalKey::Down || key == LogicalKey::Right ||
      key == LogicalKey::Home || key == LogicalKey::End) {
    UIntSize target = 0;
    if (key == LogicalKey::End) {
      target = items.size() - 1;
    } else if (selected != items.end()) {
      const auto index =
          static_cast<UIntSize>(std::distance(items.begin(), selected));
      if (key == LogicalKey::Up || key == LogicalKey::Left) {
        target = index == 0 ? 0 : index - 1;
      } else if (key == LogicalKey::Down || key == LogicalKey::Right) {
        target = std::min(items.size() - 1, index + 1);
      }
    }
    m_typeAheadPrefix.clear();
    return ActivateListItem(listHandle, items[target]);
  }

  if (!IsPrintableKey(key) || HasCommandModifier(event.modifiers) ||
      HasKeyModifier(event.modifiers, KeyModifierFlags::Alt)) {
    return {};
  }

  const auto now = std::chrono::steady_clock::now();
  constexpr auto timeout = std::chrono::milliseconds{750};
  if (m_typeAheadTime.time_since_epoch().count() == 0 ||
      now - m_typeAheadTime > timeout) {
    m_typeAheadPrefix.clear();
  }
  m_typeAheadTime = now;
  auto character = static_cast<char>(
      std::tolower(static_cast<unsigned char>(static_cast<UInt32>(key))));
  if (!(m_typeAheadPrefix.size() == 1 &&
        m_typeAheadPrefix.front() == character)) {
    m_typeAheadPrefix.push_back(character);
  }

  const auto selectedIndex =
      selected == items.end()
          ? items.size() - 1
          : static_cast<UIntSize>(std::distance(items.begin(), selected));
  for (UIntSize offset = 1; offset <= items.size(); ++offset) {
    const auto index = (selectedIndex + offset) % items.size();
    const auto *item = m_tree.Get(items[index]);
    if (item == nullptr) {
      continue;
    }
    auto label = item->properties.semantics.label.View();
    if (label.size() < m_typeAheadPrefix.size()) {
      continue;
    }
    bool matches = true;
    for (UIntSize characterIndex = 0; characterIndex < m_typeAheadPrefix.size();
         ++characterIndex) {
      const auto labelCharacter = static_cast<char>(
          std::tolower(static_cast<unsigned char>(label[characterIndex])));
      if (labelCharacter != m_typeAheadPrefix[characterIndex]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return ActivateListItem(listHandle, items[index]);
    }
  }
  return InputDispatchResult{.handled = true};
}

auto InputRouter::RouteTabKey(const ElementHandle tabHandle,
                              const KeyChanged &event) -> InputDispatchResult {
  if (event.state == KeyState::Released) {
    return {};
  }
  const auto key = event.Logical();
  if (key != LogicalKey::Left && key != LogicalKey::Right &&
      key != LogicalKey::Up && key != LogicalKey::Down &&
      key != LogicalKey::Home && key != LogicalKey::End) {
    return {};
  }
  const auto *tab = m_tree.Get(tabHandle);
  const auto *parent = tab != nullptr ? m_tree.Get(tab->parent) : nullptr;
  if (parent == nullptr) {
    return {};
  }
  std::vector<ElementHandle> tabs;
  for (const auto child : parent->children) {
    const auto *candidate = m_tree.Get(child);
    if (candidate != nullptr && candidate->type == ElementType::Tab &&
        candidate->properties.interaction.enabled &&
        candidate->properties.visibility == ElementVisibility::Visible) {
      tabs.push_back(child);
    }
  }
  const auto current = std::find(tabs.begin(), tabs.end(), tabHandle);
  if (current == tabs.end() || tabs.empty()) {
    return {};
  }
  auto index = static_cast<UIntSize>(std::distance(tabs.begin(), current));
  if (key == LogicalKey::Home) {
    index = 0;
  } else if (key == LogicalKey::End) {
    index = tabs.size() - 1;
  } else if (key == LogicalKey::Left || key == LogicalKey::Up) {
    index = index == 0 ? tabs.size() - 1 : index - 1;
  } else {
    index = (index + 1) % tabs.size();
  }

  auto result = InputDispatchResult{
      .handled = true,
      .visualStateChanged = SetFocus(tabs[index]),
      .activated = true,
  };
  if (auto *next = m_tree.Get(tabs[index]);
      next != nullptr && next->properties.interaction.onActivate) {
    next->properties.interaction.onActivate();
    result.callbackInvoked = true;
  }
  return result;
}

auto InputRouter::RouteMenuKey(const ElementHandle itemHandle,
                               const KeyChanged &event) -> InputDispatchResult {
  if (event.state == KeyState::Released) {
    return {};
  }
  const auto key = event.Logical();
  if (key != LogicalKey::Up && key != LogicalKey::Down &&
      key != LogicalKey::Home && key != LogicalKey::End) {
    return {};
  }
  const auto *item = m_tree.Get(itemHandle);
  const auto *parent = item != nullptr ? m_tree.Get(item->parent) : nullptr;
  if (parent == nullptr) {
    return {};
  }
  std::vector<ElementHandle> items;
  for (const auto child : parent->children) {
    const auto *candidate = m_tree.Get(child);
    if (candidate != nullptr && candidate->type == ElementType::MenuItem &&
        candidate->properties.interaction.enabled &&
        candidate->properties.visibility == ElementVisibility::Visible) {
      items.push_back(child);
    }
  }
  const auto current = std::find(items.begin(), items.end(), itemHandle);
  if (current == items.end() || items.empty()) {
    return {};
  }
  auto index = static_cast<UIntSize>(std::distance(items.begin(), current));
  if (key == LogicalKey::Home) {
    index = 0;
  } else if (key == LogicalKey::End) {
    index = items.size() - 1;
  } else if (key == LogicalKey::Up) {
    index = index == 0 ? items.size() - 1 : index - 1;
  } else {
    index = (index + 1) % items.size();
  }
  return InputDispatchResult{
      .handled = true,
      .visualStateChanged = SetFocus(items[index]),
  };
}

auto InputRouter::RouteKey(const KeyChanged &event) -> InputDispatchResult {
  if (event.state == KeyState::Pressed &&
      event.Logical() == LogicalKey::Escape) {
    const auto popup = TopPopup();
    const auto *popupNode = m_tree.Get(popup);
    if (popupNode != nullptr && popupNode->properties.popup.dismissOnEscape) {
      InputDispatchResult result{.handled = true};
      if (popupNode->properties.popup.onDismiss) {
        popupNode->properties.popup.onDismiss();
        result.callbackInvoked = true;
      }
      return result;
    }
  }

  RoutedKeyEvent routed{
      .physicalKey = event.physicalKey,
      .logicalKey = event.Logical(),
      .state = event.state,
      .modifiers = event.modifiers,
  };
  auto result = Dispatch(routed, FocusedElement());

  auto *focused = m_tree.Get(FocusedElement());
  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::TextField &&
      focused->properties.interaction.enabled) {
    const auto textFieldResult = RouteTextFieldKey(*focused, event);
    result.handled = textFieldResult.handled;
    result.visualStateChanged = textFieldResult.visualStateChanged;
    result.layoutStateChanged = textFieldResult.layoutStateChanged;
    result.callbackInvoked =
        result.callbackInvoked || textFieldResult.callbackInvoked;
  }

  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::ListView &&
      focused->properties.interaction.enabled) {
    const auto listResult = RouteListKey(focused->handle, event);
    result.handled = listResult.handled;
    result.visualStateChanged =
        result.visualStateChanged || listResult.visualStateChanged;
    result.layoutStateChanged =
        result.layoutStateChanged || listResult.layoutStateChanged;
    result.callbackInvoked =
        result.callbackInvoked || listResult.callbackInvoked;
    result.activated = result.activated || listResult.activated;
  }

  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::Tab &&
      focused->properties.interaction.enabled) {
    const auto tabResult = RouteTabKey(focused->handle, event);
    result.handled = tabResult.handled;
    result.visualStateChanged =
        result.visualStateChanged || tabResult.visualStateChanged;
    result.callbackInvoked =
        result.callbackInvoked || tabResult.callbackInvoked;
    result.activated = result.activated || tabResult.activated;
  }

  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::MenuItem &&
      focused->properties.interaction.enabled) {
    const auto menuResult = RouteMenuKey(focused->handle, event);
    result.handled = menuResult.handled;
    result.visualStateChanged =
        result.visualStateChanged || menuResult.visualStateChanged;
  }

  if (!result.handled && focused != nullptr && IsScrollable(focused->type) &&
      focused->properties.interaction.enabled &&
      event.state != KeyState::Released) {
    const auto previous = focused->scroll.offset;
    const auto horizontalMaximum =
        std::max(0.0F, focused->scroll.contentSize.width -
                           focused->scroll.viewportSize.width);
    const auto verticalMaximum =
        std::max(0.0F, focused->scroll.contentSize.height -
                           focused->scroll.viewportSize.height);
    const auto step = std::max(1.0F, focused->properties.scroll.wheelStep);
    switch (event.Logical()) {
    case LogicalKey::Left:
      if (focused->properties.scroll.horizontal) {
        focused->scroll.offset.x =
            std::max(0.0F, focused->scroll.offset.x - step);
      }
      break;
    case LogicalKey::Right:
      if (focused->properties.scroll.horizontal) {
        focused->scroll.offset.x =
            std::min(horizontalMaximum, focused->scroll.offset.x + step);
      }
      break;
    case LogicalKey::Up:
      if (focused->properties.scroll.vertical) {
        focused->scroll.offset.y =
            std::max(0.0F, focused->scroll.offset.y - step);
      }
      break;
    case LogicalKey::Down:
      if (focused->properties.scroll.vertical) {
        focused->scroll.offset.y =
            std::min(verticalMaximum, focused->scroll.offset.y + step);
      }
      break;
    case LogicalKey::Home:
      focused->scroll.offset = {};
      break;
    case LogicalKey::End:
      focused->scroll.offset = Point{horizontalMaximum, verticalMaximum};
      break;
    default:
      break;
    }
    if (focused->scroll.offset != previous) {
      result.handled = true;
      result.layoutStateChanged = true;
      result.visualStateChanged = true;
    }
  }

  if (!result.handled && event.state == KeyState::Pressed &&
      event.Logical() == LogicalKey::Tab) {
    result.visualStateChanged =
        MoveFocus(HasKeyModifier(event.modifiers, KeyModifierFlags::Shift));
    result.handled = true;
    return result;
  }

  const auto activatesButton = event.Logical() == LogicalKey::Enter ||
                               event.Logical() == LogicalKey::Space;
  if (!result.handled && focused != nullptr && IsActivatable(focused->type) &&
      focused->properties.interaction.enabled && activatesButton) {
    if (event.state == KeyState::Pressed && !focused->interaction.pressed) {
      focused->interaction.pressed = true;
      focused->interaction.keyboardPressed = true;
      result.visualStateChanged = true;
      result.handled = true;
    } else if (event.state == KeyState::Released &&
               focused->interaction.keyboardPressed) {
      focused->interaction.pressed = false;
      focused->interaction.keyboardPressed = false;
      result.visualStateChanged = true;
      result.handled = true;
      if (focused->properties.interaction.onActivate) {
        focused->properties.interaction.onActivate();
        result.callbackInvoked = true;
        result.activated = true;
      }
    }
  }
  if (!result.handled && TopModalPopup()) {
    result.handled = true;
  }
  return result;
}

auto InputRouter::RouteText(const TextInput &event) -> InputDispatchResult {
  RoutedTextEvent routed{
      .eventKind = RoutedTextEventKind::Input,
      .text = event.text,
  };
  auto result = Dispatch(routed, FocusedElement());
  auto *focused = m_tree.Get(FocusedElement());
  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::TextField &&
      focused->properties.interaction.enabled) {
    const auto textFieldResult = RouteTextFieldInput(*focused, event.text);
    result.handled = textFieldResult.handled;
    result.visualStateChanged = textFieldResult.visualStateChanged;
    result.layoutStateChanged = textFieldResult.layoutStateChanged;
    result.callbackInvoked =
        result.callbackInvoked || textFieldResult.callbackInvoked;
  }
  if (!result.handled && TopModalPopup()) {
    result.handled = true;
  }
  return result;
}

auto InputRouter::RouteText(const TextComposition &event)
    -> InputDispatchResult {
  RoutedTextEvent routed{
      .eventKind = RoutedTextEventKind::Composition,
      .text = event.text,
      .selectionStart = event.selectionStart,
      .selectionLength = event.selectionLength,
  };
  auto result = Dispatch(routed, FocusedElement());
  auto *focused = m_tree.Get(FocusedElement());
  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::TextField &&
      focused->properties.interaction.enabled) {
    const auto textFieldResult = RouteTextFieldComposition(*focused, event);
    result.handled = textFieldResult.handled;
    result.visualStateChanged = textFieldResult.visualStateChanged;
    result.layoutStateChanged = textFieldResult.layoutStateChanged;
    result.callbackInvoked =
        result.callbackInvoked || textFieldResult.callbackInvoked;
  }
  if (!result.handled && TopModalPopup()) {
    result.handled = true;
  }
  return result;
}

auto InputRouter::UpdateHover(const UInt64 pointerId,
                              const PointerKind pointerKind,
                              const Point position) -> InputDispatchResult {
  InputDispatchResult result{};
  const auto previous = HoveredElement(pointerId);
  const auto next = HitTest(position);
  if (previous == next) {
    return result;
  }

  if (auto *node = m_tree.Get(previous); node != nullptr) {
    node->interaction.hovered = false;
    RoutedPointerEvent exited{
        .eventKind = RoutedPointerEventKind::Exited,
        .pointerId = pointerId,
        .pointerKind = pointerKind,
        .position = position,
    };
    const auto outcome = Dispatch(exited, previous);
    result.handled = result.handled || outcome.handled;
    result.callbackInvoked = result.callbackInvoked || outcome.callbackInvoked;
  }

  if (auto *node = m_tree.Get(next); node != nullptr) {
    node->interaction.hovered = true;
    m_hovered[pointerId] = next;
    RoutedPointerEvent entered{
        .eventKind = RoutedPointerEventKind::Entered,
        .pointerId = pointerId,
        .pointerKind = pointerKind,
        .position = position,
    };
    const auto outcome = Dispatch(entered, next);
    result.handled = result.handled || outcome.handled;
    result.callbackInvoked = result.callbackInvoked || outcome.callbackInvoked;
  } else {
    m_hovered.erase(pointerId);
  }

  result.visualStateChanged = true;
  return result;
}

auto InputRouter::RouteMoved(const PointerMoved &event) -> InputDispatchResult {
  auto result = UpdateHover(event.pointerId, event.kind, event.position);
  const auto captured = CapturedElement(event.pointerId);
  if (auto *scroll = m_tree.Get(captured);
      scroll != nullptr && IsScrollable(scroll->type) &&
      scroll->scroll.dragPointerId == event.pointerId &&
      (scroll->scroll.draggingHorizontal || scroll->scroll.draggingVertical)) {
    result.handled = true;
    const auto bars = Detail::ComputeScrollBars(
        scroll->arrangedBounds, scroll->properties.scroll, scroll->scroll);
    const auto previous = scroll->scroll.offset;
    if (scroll->scroll.draggingHorizontal && bars.hasHorizontal) {
      const auto travel =
          bars.horizontalTrack.width - bars.horizontalThumb.width;
      const auto maximum =
          std::max(0.0F, scroll->scroll.contentSize.width -
                             scroll->scroll.viewportSize.width);
      if (travel > 0.0F) {
        scroll->scroll.offset.x =
            std::clamp(scroll->scroll.dragOffset.x +
                           (event.position.x - scroll->scroll.dragOrigin.x) *
                               maximum / travel,
                       0.0F, maximum);
      }
    }
    if (scroll->scroll.draggingVertical && bars.hasVertical) {
      const auto travel = bars.verticalTrack.height - bars.verticalThumb.height;
      const auto maximum =
          std::max(0.0F, scroll->scroll.contentSize.height -
                             scroll->scroll.viewportSize.height);
      if (travel > 0.0F) {
        scroll->scroll.offset.y =
            std::clamp(scroll->scroll.dragOffset.y +
                           (event.position.y - scroll->scroll.dragOrigin.y) *
                               maximum / travel,
                       0.0F, maximum);
      }
    }
    if (scroll->scroll.offset != previous) {
      result.layoutStateChanged = true;
      result.visualStateChanged = true;
    }
  }
  const auto target = captured ? captured : HitTest(event.position);
  RoutedPointerEvent routed{
      .eventKind = RoutedPointerEventKind::Moved,
      .pointerId = event.pointerId,
      .pointerKind = event.kind,
      .position = event.position,
  };
  const auto outcome = Dispatch(routed, target);
  result.handled = result.handled || outcome.handled;
  result.callbackInvoked = result.callbackInvoked || outcome.callbackInvoked;
  if (outcome.captureRequest) {
    result.visualStateChanged =
        SetCaptured(event.pointerId, outcome.captureRequest) ||
        result.visualStateChanged;
  }
  if (outcome.releaseCapture) {
    result.visualStateChanged =
        ReleaseCaptured(event.pointerId) || result.visualStateChanged;
  }
  return result;
}

auto InputRouter::RouteButton(const PointerButtonChanged &event)
    -> InputDispatchResult {
  auto result = UpdateHover(event.pointerId, event.kind, event.position);
  const auto hit = HitTest(event.position);
  const auto popup = TopPopup();
  const auto *popupNode = m_tree.Get(popup);
  if (event.state == ButtonState::Pressed &&
      event.button == PointerButton::Primary && popupNode != nullptr &&
      !IsWithin(hit, popup)) {
    if (popupNode->properties.popup.dismissOnOutsidePointer) {
      result.handled = true;
      if (popupNode->properties.popup.onDismiss) {
        popupNode->properties.popup.onDismiss();
        result.callbackInvoked = true;
      }
      return result;
    }
    if (popupNode->properties.popup.modal) {
      result.handled = true;
      return result;
    }
  }
  const auto captured = CapturedElement(event.pointerId);
  if (event.state == ButtonState::Released &&
      event.button == PointerButton::Primary) {
    if (auto *scroll = m_tree.Get(captured);
        scroll != nullptr && IsScrollable(scroll->type) &&
        scroll->scroll.dragPointerId == event.pointerId) {
      scroll->scroll.dragPointerId = 0;
      scroll->scroll.draggingHorizontal = false;
      scroll->scroll.draggingVertical = false;
      result.handled = true;
      result.visualStateChanged =
          ReleaseCaptured(event.pointerId) || result.visualStateChanged;
      return result;
    }
  }

  if (event.state == ButtonState::Pressed &&
      event.button == PointerButton::Primary) {
    auto scrollHandle = hit;
    while (auto *scroll = m_tree.Get(scrollHandle)) {
      if (IsScrollable(scroll->type) &&
          scroll->properties.interaction.enabled) {
        auto bars = Detail::ComputeScrollBars(
            scroll->arrangedBounds, scroll->properties.scroll, scroll->scroll);
        const auto horizontal =
            bars.hasHorizontal && bars.horizontalTrack.Contains(event.position);
        const auto vertical =
            bars.hasVertical && bars.verticalTrack.Contains(event.position);
        if (horizontal || vertical) {
          if (horizontal && !bars.horizontalThumb.Contains(event.position)) {
            const auto travel =
                bars.horizontalTrack.width - bars.horizontalThumb.width;
            const auto maximum =
                std::max(0.0F, scroll->scroll.contentSize.width -
                                   scroll->scroll.viewportSize.width);
            if (travel > 0.0F) {
              scroll->scroll.offset.x =
                  std::clamp((event.position.x - bars.horizontalTrack.x -
                              bars.horizontalThumb.width * 0.5F) *
                                 maximum / travel,
                             0.0F, maximum);
            }
          }
          if (vertical && !bars.verticalThumb.Contains(event.position)) {
            const auto travel =
                bars.verticalTrack.height - bars.verticalThumb.height;
            const auto maximum =
                std::max(0.0F, scroll->scroll.contentSize.height -
                                   scroll->scroll.viewportSize.height);
            if (travel > 0.0F) {
              scroll->scroll.offset.y =
                  std::clamp((event.position.y - bars.verticalTrack.y -
                              bars.verticalThumb.height * 0.5F) *
                                 maximum / travel,
                             0.0F, maximum);
            }
          }
          scroll->scroll.dragPointerId = event.pointerId;
          scroll->scroll.dragOrigin = event.position;
          scroll->scroll.dragOffset = scroll->scroll.offset;
          scroll->scroll.draggingHorizontal = horizontal;
          scroll->scroll.draggingVertical = vertical;
          static_cast<void>(SetCaptured(event.pointerId, scrollHandle));
          if (scroll->properties.interaction.focusable) {
            static_cast<void>(SetFocus(scrollHandle));
          }
          result.handled = true;
          result.layoutStateChanged = true;
          result.visualStateChanged = true;
          return result;
        }
      }
      scrollHandle = scroll->parent;
    }
  }
  const auto activatable = ActivatableAncestor(hit);
  const auto target = captured ? captured : (activatable ? activatable : hit);
  auto *node = m_tree.Get(target);

  if (event.state == ButtonState::Pressed &&
      event.button == PointerButton::Primary) {
    if (node != nullptr && node->properties.interaction.enabled) {
      if (IsActivatable(node->type)) {
        node->interaction.pressed = true;
        static_cast<void>(SetCaptured(event.pointerId, target));
        result.visualStateChanged = true;
      }
      const auto focusTarget = FocusableAncestor(target);
      if (focusTarget) {
        result.visualStateChanged =
            SetFocus(focusTarget) || result.visualStateChanged;
      } else {
        result.visualStateChanged = ClearFocus() || result.visualStateChanged;
      }
    } else {
      result.visualStateChanged = ClearFocus() || result.visualStateChanged;
    }
  }

  RoutedPointerEvent routed{
      .eventKind = event.state == ButtonState::Pressed
                       ? RoutedPointerEventKind::ButtonPressed
                       : RoutedPointerEventKind::ButtonReleased,
      .pointerId = event.pointerId,
      .pointerKind = event.kind,
      .button = event.button,
      .position = event.position,
  };
  const auto outcome = Dispatch(routed, target);
  result.handled = result.handled || outcome.handled;
  result.callbackInvoked = result.callbackInvoked || outcome.callbackInvoked;
  if (outcome.captureRequest) {
    result.visualStateChanged =
        SetCaptured(event.pointerId, outcome.captureRequest) ||
        result.visualStateChanged;
  }

  node = m_tree.Get(target);
  if (event.state == ButtonState::Released &&
      event.button == PointerButton::Primary && node != nullptr &&
      IsActivatable(node->type) && node->interaction.pressed) {
    node->interaction.pressed = false;
    result.visualStateChanged = true;
    if ((hit == target || IsWithin(hit, target)) &&
        node->properties.interaction.enabled &&
        node->properties.interaction.onActivate) {
      node->properties.interaction.onActivate();
      result.callbackInvoked = true;
      result.activated = true;
      result.handled = true;
    }
    result.visualStateChanged =
        ReleaseCaptured(event.pointerId) || result.visualStateChanged;
  } else if (outcome.releaseCapture) {
    result.visualStateChanged =
        ReleaseCaptured(event.pointerId) || result.visualStateChanged;
  }

  return result;
}

auto InputRouter::RouteWheel(const PointerWheelChanged &event)
    -> InputDispatchResult {
  auto result =
      UpdateHover(event.pointerId, PointerKind::Mouse, event.position);
  const auto captured = CapturedElement(event.pointerId);
  const auto target = captured ? captured : HitTest(event.position);
  RoutedPointerEvent routed{
      .eventKind = RoutedPointerEventKind::Wheel,
      .pointerId = event.pointerId,
      .pointerKind = PointerKind::Mouse,
      .position = event.position,
      .wheelDelta = event.delta,
  };
  const auto outcome = Dispatch(routed, target);
  result.handled = outcome.handled;
  result.callbackInvoked = result.callbackInvoked || outcome.callbackInvoked;
  if (outcome.captureRequest) {
    result.visualStateChanged =
        SetCaptured(event.pointerId, outcome.captureRequest) ||
        result.visualStateChanged;
  }
  if (outcome.releaseCapture) {
    result.visualStateChanged =
        ReleaseCaptured(event.pointerId) || result.visualStateChanged;
  }
  if (result.handled) {
    return result;
  }
  if (!target && TopModalPopup()) {
    result.handled = true;
    return result;
  }

  auto current = target;
  while (auto *node = m_tree.Get(current)) {
    if (IsScrollable(node->type) && node->properties.interaction.enabled) {
      const auto previous = node->scroll.offset;
      const auto step = std::max(0.0F, node->properties.scroll.wheelStep);
      if (node->properties.scroll.horizontal) {
        node->scroll.offset.x =
            std::clamp(node->scroll.offset.x - event.delta.x * step, 0.0F,
                       std::max(0.0F, node->scroll.contentSize.width -
                                          node->scroll.viewportSize.width));
      }
      if (node->properties.scroll.vertical) {
        node->scroll.offset.y =
            std::clamp(node->scroll.offset.y - event.delta.y * step, 0.0F,
                       std::max(0.0F, node->scroll.contentSize.height -
                                          node->scroll.viewportSize.height));
      }
      if (node->scroll.offset != previous) {
        result.handled = true;
        result.visualStateChanged = true;
        result.layoutStateChanged = true;
        break;
      }
    }
    current = node->parent;
  }
  return result;
}

auto InputRouter::SetCaptured(const UInt64 pointerId,
                              const ElementHandle handle) noexcept -> bool {
  if (!m_tree.IsAlive(handle) || CapturedElement(pointerId) == handle) {
    return false;
  }
  m_captured[pointerId] = handle;
  return true;
}

auto InputRouter::ReleaseCaptured(const UInt64 pointerId) noexcept -> bool {
  return m_captured.erase(pointerId) > 0;
}
} // namespace NGIN::UI
