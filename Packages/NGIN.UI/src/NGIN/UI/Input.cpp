#include <NGIN/UI/Input.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace NGIN::UI {
InputRouter::InputRouter(RuntimeTree &tree) noexcept : m_tree(tree) {}

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
  return std::visit(
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
      !next->properties.interaction.focusable) {
    return false;
  }

  if (auto *previous = m_tree.Get(m_focused); previous != nullptr) {
    previous->interaction.focused = false;
    if (previous->interaction.keyboardPressed) {
      previous->interaction.keyboardPressed = false;
      previous->interaction.pressed = false;
    }
  }
  m_focused = handle;
  next->interaction.focused = true;
  return true;
}

auto InputRouter::ClearFocus() noexcept -> bool {
  auto *focused = m_tree.Get(m_focused);
  if (focused == nullptr) {
    m_focused = {};
    return false;
  }
  focused->interaction.focused = false;
  if (focused->interaction.keyboardPressed) {
    focused->interaction.keyboardPressed = false;
    focused->interaction.pressed = false;
  }
  m_focused = {};
  return true;
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
  const auto focusNeedsRestoration = !m_tree.IsAlive(m_focused);
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
    if (!node->properties.interaction.enabled) {
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
    if (!node->properties.interaction.hitTestVisible) {
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
  if (node == nullptr) {
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
  if (node == nullptr || !node->properties.interaction.onPointer) {
    return;
  }

  event.phase = phase;
  event.currentTarget = handle;
  event.captureRequested = false;
  event.captureReleaseRequested = false;
  node->properties.interaction.onPointer(event);
  outcome.callbackInvoked = true;
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
    if (node == nullptr || !node->properties.interaction.onKey) {
      return;
    }
    event.phase = phase;
    event.currentTarget = handle;
    node->properties.interaction.onKey(event);
    result.callbackInvoked = true;
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
    if (node == nullptr || !node->properties.interaction.onText) {
      return;
    }
    event.phase = phase;
    event.currentTarget = handle;
    node->properties.interaction.onText(event);
    result.callbackInvoked = true;
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
    if (node == nullptr) {
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

  if (!result.handled && event.state == KeyState::Pressed &&
      event.Logical() == LogicalKey::Tab) {
    result.visualStateChanged =
        MoveFocus(HasKeyModifier(event.modifiers, KeyModifierFlags::Shift));
    result.handled = true;
    return result;
  }

  auto *focused = m_tree.Get(FocusedElement());
  const auto activatesButton = event.Logical() == LogicalKey::Enter ||
                               event.Logical() == LogicalKey::Space;
  if (!result.handled && focused != nullptr &&
      focused->type == ElementType::Button &&
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
  const auto target = captured ? captured : hit;
  auto *node = m_tree.Get(target);

  if (event.state == ButtonState::Pressed &&
      event.button == PointerButton::Primary) {
    if (node != nullptr && node->properties.interaction.enabled) {
      if (node->type == ElementType::Button) {
        node->interaction.pressed = true;
        static_cast<void>(SetCaptured(event.pointerId, target));
        result.visualStateChanged = true;
      }
      if (node->properties.interaction.focusable) {
        result.visualStateChanged =
            SetFocus(target) || result.visualStateChanged;
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
      node->type == ElementType::Button && node->interaction.pressed) {
    node->interaction.pressed = false;
    result.visualStateChanged = true;
    if (hit == target && node->properties.interaction.enabled &&
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
    if (node->type == ElementType::ScrollView &&
        node->properties.interaction.enabled) {
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
