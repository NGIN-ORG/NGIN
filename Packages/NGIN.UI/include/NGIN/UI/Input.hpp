#pragma once

#include <NGIN/UI/Events.hpp>
#include <NGIN/UI/Platform.hpp>
#include <NGIN/UI/RuntimeTree.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <unordered_map>
#include <vector>

namespace NGIN::UI {
struct InputDispatchResult final {
  bool handled{false};
  bool visualStateChanged{false};
  bool layoutStateChanged{false};
  bool callbackInvoked{false};
  bool activated{false};
};

class InputRouter final {
public:
  explicit InputRouter(RuntimeTree &tree,
                       IPlatformBackend *platform = nullptr) noexcept;

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
  auto MoveFocus(bool reverse = false) -> bool;
  void Synchronize() noexcept;

private:
  struct PopupSession final {
    ElementHandle handle{};
    ElementHandle restoreFocus{};
  };

  struct DispatchOutcome final {
    bool handled{false};
    bool callbackInvoked{false};
    ElementHandle captureRequest{};
    bool releaseCapture{false};
  };

  [[nodiscard]] auto HitTestSubtree(ElementHandle handle, Point position,
                                    ElementHandle popupRoot = {}) const noexcept
      -> ElementHandle;
  void CollectPopups(ElementHandle handle,
                     std::vector<ElementHandle> &popups) const;
  [[nodiscard]] auto IsWithin(ElementHandle handle,
                              ElementHandle ancestor) const noexcept -> bool;
  [[nodiscard]] auto TopPopup() const noexcept -> ElementHandle;
  [[nodiscard]] auto TopModalPopup() const noexcept -> ElementHandle;
  [[nodiscard]] auto BuildPath(ElementHandle target) const
      -> std::vector<ElementHandle>;
  auto Dispatch(RoutedPointerEvent &event, ElementHandle target)
      -> DispatchOutcome;
  void InvokeHandler(ElementHandle handle, EventPhase phase,
                     RoutedPointerEvent &event, DispatchOutcome &outcome) const;
  auto Dispatch(RoutedKeyEvent &event, ElementHandle target)
      -> InputDispatchResult;
  auto Dispatch(RoutedTextEvent &event, ElementHandle target)
      -> InputDispatchResult;
  [[nodiscard]] auto FocusCandidates(ElementHandle scope = {}) const
      -> std::vector<ElementHandle>;
  auto RouteKey(const KeyChanged &event) -> InputDispatchResult;
  auto RouteText(const TextInput &event) -> InputDispatchResult;
  auto RouteText(const TextComposition &event) -> InputDispatchResult;
  auto RouteTextFieldKey(RuntimeNode &node, const KeyChanged &event)
      -> InputDispatchResult;
  auto RouteTextFieldInput(RuntimeNode &node, const NGIN::Text::String &text)
      -> InputDispatchResult;
  auto CommitTextFieldEdit(
      RuntimeNode &node,
      NGIN::Utilities::Callable<UIResult<void>(TextEditingBuffer &)> edit)
      -> InputDispatchResult;
  void ReportTextFieldError(const RuntimeNode &node,
                            const UIError &error) const;
  auto UpdateHover(UInt64 pointerId, PointerKind pointerKind, Point position)
      -> InputDispatchResult;
  auto RouteMoved(const PointerMoved &event) -> InputDispatchResult;
  auto RouteButton(const PointerButtonChanged &event) -> InputDispatchResult;
  auto RouteWheel(const PointerWheelChanged &event) -> InputDispatchResult;
  auto SetCaptured(UInt64 pointerId, ElementHandle handle) noexcept -> bool;
  auto ReleaseCaptured(UInt64 pointerId) noexcept -> bool;

  RuntimeTree &m_tree;
  IPlatformBackend *m_platform{nullptr};
  ElementHandle m_focused{};
  std::unordered_map<UInt64, ElementHandle> m_captured{};
  std::unordered_map<UInt64, ElementHandle> m_hovered{};
  std::vector<PopupSession> m_popups{};
};
} // namespace NGIN::UI
