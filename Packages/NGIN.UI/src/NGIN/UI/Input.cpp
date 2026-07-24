#include <NGIN/UI/Input.hpp>

#include <algorithm>
#include <type_traits>

namespace NGIN::UI {
InputRouter::InputRouter(RuntimeTree &tree) noexcept : m_tree(tree) {}

auto InputRouter::HitTest(const Point position) const noexcept
    -> ElementHandle {
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

auto InputRouter::Route(const PlatformEvent &event) -> InputDispatchResult {
  return std::visit(
      [this](const auto &value) -> InputDispatchResult {
        using Event = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, PointerMoved>) {
          return RouteMoved(value);
        } else if constexpr (std::is_same_v<Event, PointerButtonChanged>) {
          return RouteButton(value);
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

  auto *next = m_tree.Get(handle);
  if (next == nullptr || !next->properties.interaction.enabled ||
      !next->properties.interaction.focusable) {
    return false;
  }

  if (auto *previous = m_tree.Get(m_focused); previous != nullptr) {
    previous->interaction.focused = false;
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
  m_focused = {};
  return true;
}

void InputRouter::Synchronize() noexcept {
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
    return false;
  });
}

auto InputRouter::HitTestSubtree(const ElementHandle handle,
                                 const Point position) const noexcept
    -> ElementHandle {
  const auto *node = m_tree.Get(handle);
  if (node == nullptr || !node->properties.interaction.hitTestVisible ||
      !node->arrangedBounds.Contains(position)) {
    return {};
  }

  for (auto child = node->children.rbegin(); child != node->children.rend();
       ++child) {
    if (const auto hit = HitTestSubtree(*child, position); hit) {
      return hit;
    }
  }
  return handle == m_tree.Root() ? ElementHandle{} : handle;
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
