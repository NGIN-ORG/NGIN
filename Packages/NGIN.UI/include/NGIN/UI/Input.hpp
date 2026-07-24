#pragma once

#include <NGIN/UI/Events.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

#include <unordered_map>
#include <vector>

namespace NGIN::UI {
struct InputDispatchResult final {
  bool handled{false};
  bool visualStateChanged{false};
  bool callbackInvoked{false};
  bool activated{false};
};

class InputRouter final {
public:
  explicit InputRouter(RuntimeTree &tree) noexcept;

  [[nodiscard]] auto HitTest(Point position) const noexcept -> ElementHandle;
  [[nodiscard]] auto FocusedElement() const noexcept -> ElementHandle;
  [[nodiscard]] auto CapturedElement(UInt64 pointerId) const noexcept
      -> ElementHandle;
  [[nodiscard]] auto HoveredElement(UInt64 pointerId) const noexcept
      -> ElementHandle;
  [[nodiscard]] auto FirstCapturedElement() const noexcept -> ElementHandle;

  auto Route(const PlatformEvent &event) -> InputDispatchResult;
  auto SetFocus(ElementHandle handle) noexcept -> bool;
  auto ClearFocus() noexcept -> bool;
  void Synchronize() noexcept;

private:
  struct DispatchOutcome final {
    bool handled{false};
    bool callbackInvoked{false};
    ElementHandle captureRequest{};
    bool releaseCapture{false};
  };

  [[nodiscard]] auto HitTestSubtree(ElementHandle handle,
                                    Point position) const noexcept
      -> ElementHandle;
  [[nodiscard]] auto BuildPath(ElementHandle target) const
      -> std::vector<ElementHandle>;
  auto Dispatch(RoutedPointerEvent &event, ElementHandle target)
      -> DispatchOutcome;
  void InvokeHandler(ElementHandle handle, EventPhase phase,
                     RoutedPointerEvent &event, DispatchOutcome &outcome) const;
  auto UpdateHover(UInt64 pointerId, PointerKind pointerKind, Point position)
      -> InputDispatchResult;
  auto RouteMoved(const PointerMoved &event) -> InputDispatchResult;
  auto RouteButton(const PointerButtonChanged &event) -> InputDispatchResult;
  auto SetCaptured(UInt64 pointerId, ElementHandle handle) noexcept -> bool;
  auto ReleaseCaptured(UInt64 pointerId) noexcept -> bool;

  RuntimeTree &m_tree;
  ElementHandle m_focused{};
  std::unordered_map<UInt64, ElementHandle> m_captured{};
  std::unordered_map<UInt64, ElementHandle> m_hovered{};
};
} // namespace NGIN::UI
