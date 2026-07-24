#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Events.hpp>

namespace NGIN::UI {
enum class EventPhase : UInt8 {
  Capture,
  Target,
  Bubble,
};

enum class RoutedPointerEventKind : UInt8 {
  Entered,
  Exited,
  Moved,
  ButtonPressed,
  ButtonReleased,
};

struct RoutedPointerEvent final {
  EventPhase phase{EventPhase::Target};
  RoutedPointerEventKind eventKind{RoutedPointerEventKind::Moved};
  ElementHandle target{};
  ElementHandle currentTarget{};
  UInt64 pointerId{0};
  PointerKind pointerKind{PointerKind::Mouse};
  PointerButton button{PointerButton::None};
  Point position{};
  bool handled{false};
  bool captureRequested{false};
  bool captureReleaseRequested{false};

  void Handle() noexcept { handled = true; }
  void CapturePointer() noexcept { captureRequested = true; }
  void ReleasePointerCapture() noexcept { captureReleaseRequested = true; }
};
} // namespace NGIN::UI
