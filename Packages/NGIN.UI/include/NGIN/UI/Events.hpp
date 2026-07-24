#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>

#include <type_traits>
#include <variant>
#include <vector>

namespace NGIN::UI {
enum class PointerKind : UInt8 {
  Mouse,
  Touch,
  Pen,
};

enum class PointerButton : UInt8 {
  None,
  Primary,
  Secondary,
  Middle,
  Auxiliary1,
  Auxiliary2,
};

enum class ButtonState : UInt8 {
  Released,
  Pressed,
};

enum class KeyState : UInt8 {
  Released,
  Pressed,
  Repeated,
};

enum class ThemePreference : UInt8 {
  System,
  Light,
  Dark,
  HighContrast,
};

struct WindowCloseRequested final {
  PlatformWindowHandle window{};
};

struct WindowResized final {
  PlatformWindowHandle window{};
  PixelSize size{};
};

struct WindowMoved final {
  PlatformWindowHandle window{};
  PixelPoint position{};
};

struct WindowScaleChanged final {
  PlatformWindowHandle window{};
  F32 scaleFactor{1.0F};
};

struct WindowFocusChanged final {
  PlatformWindowHandle window{};
  bool focused{false};
};

struct PointerMoved final {
  PlatformWindowHandle window{};
  UInt64 pointerId{0};
  PointerKind kind{PointerKind::Mouse};
  Point position{};
  Point delta{};
};

struct PointerButtonChanged final {
  PlatformWindowHandle window{};
  UInt64 pointerId{0};
  PointerKind kind{PointerKind::Mouse};
  PointerButton button{PointerButton::None};
  ButtonState state{ButtonState::Released};
  Point position{};
};

struct PointerWheelChanged final {
  PlatformWindowHandle window{};
  UInt64 pointerId{0};
  Point delta{};
  Point position{};
};

struct KeyChanged final {
  PlatformWindowHandle window{};
  UInt32 physicalKey{0};
  UInt32 logicalKey{0};
  KeyState state{KeyState::Released};
  UInt32 modifiers{0};
};

struct TextInput final {
  PlatformWindowHandle window{};
  NGIN::Text::String text{};
};

struct TextComposition final {
  PlatformWindowHandle window{};
  NGIN::Text::String text{};
  UIntSize selectionStart{0};
  UIntSize selectionLength{0};
};

struct FileDrop final {
  PlatformWindowHandle window{};
  std::vector<NGIN::Text::String> paths{};
  Point position{};
};

struct ThemeChanged final {
  ThemePreference preference{ThemePreference::System};
};

using PlatformEvent =
    std::variant<WindowCloseRequested, WindowResized, WindowMoved,
                 WindowScaleChanged, WindowFocusChanged, PointerMoved,
                 PointerButtonChanged, PointerWheelChanged, KeyChanged,
                 TextInput, TextComposition, FileDrop, ThemeChanged>;

[[nodiscard]] inline auto EventWindow(const PlatformEvent &event) noexcept
    -> PlatformWindowHandle {
  return std::visit(
      [](const auto &value) -> PlatformWindowHandle {
        using Event = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, ThemeChanged>) {
          return {};
        } else {
          return value.window;
        }
      },
      event);
}

class IPlatformEventSink {
public:
  virtual ~IPlatformEventSink() = default;
  virtual void Push(PlatformEvent event) = 0;
};
} // namespace NGIN::UI
