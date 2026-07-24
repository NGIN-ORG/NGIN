#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Events.hpp>

namespace NGIN::UI {
/// @brief Tunnel, target, or bubble phase of a routed event.
enum class EventPhase : UInt8 {
  Capture,
  Target,
  Bubble,
};

/// @brief Pointer transition represented by a routed pointer event.
enum class RoutedPointerEventKind : UInt8 {
  Entered,
  Exited,
  Moved,
  Wheel,
  ButtonPressed,
  ButtonReleased,
};

/// @brief Mutable pointer event routed through the runtime ancestor chain.
struct RoutedPointerEvent final {
  EventPhase phase{EventPhase::Target};
  RoutedPointerEventKind eventKind{RoutedPointerEventKind::Moved};
  ElementHandle target{};
  ElementHandle currentTarget{};
  UInt64 pointerId{0};
  PointerKind pointerKind{PointerKind::Mouse};
  PointerButton button{PointerButton::None};
  Point position{};
  Point wheelDelta{};
  bool handled{false};
  bool captureRequested{false};
  bool captureReleaseRequested{false};

  void Handle() noexcept { handled = true; }
  void CapturePointer() noexcept { captureRequested = true; }
  void ReleasePointerCapture() noexcept { captureReleaseRequested = true; }
};

/// @brief Mutable keyboard event routed through the focus ancestor chain.
struct RoutedKeyEvent final {
  EventPhase phase{EventPhase::Target};
  ElementHandle target{};
  ElementHandle currentTarget{};
  UInt32 physicalKey{0};
  LogicalKey logicalKey{};
  KeyState state{KeyState::Released};
  UInt32 modifiers{0};
  bool handled{false};

  void Handle() noexcept { handled = true; }
};

/// @brief Committed input or active composition represented by a text event.
enum class RoutedTextEventKind : UInt8 {
  Input,
  Composition,
};

/// @brief Mutable text event routed to the focused editing element.
struct RoutedTextEvent final {
  EventPhase phase{EventPhase::Target};
  RoutedTextEventKind eventKind{RoutedTextEventKind::Input};
  ElementHandle target{};
  ElementHandle currentTarget{};
  NGIN::Text::String text{};
  // Normalized UTF-8 byte offsets within text for composition events.
  UIntSize selectionStart{0};
  UIntSize selectionLength{0};
  bool handled{false};

  void Handle() noexcept { handled = true; }
};
} // namespace NGIN::UI
