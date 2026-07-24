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

enum class KeyModifierFlags : UInt32 {
  None = 0,
  Shift = 1U << 0U,
  Control = 1U << 1U,
  Alt = 1U << 2U,
  Super = 1U << 3U,
  CapsLock = 1U << 4U,
  NumLock = 1U << 5U,
};

enum class LogicalKey : UInt32 {
  Backspace = 8,
  Tab = 9,
  Enter = 13,
  Escape = 27,
  Space = 32,
  Delete = 127,
  Home = 0x00100001U,
  End = 0x00100002U,
  Left = 0x00100003U,
  Right = 0x00100004U,
  Up = 0x00100005U,
  Down = 0x00100006U,
};

[[nodiscard]] constexpr auto operator|(const KeyModifierFlags left,
                                       const KeyModifierFlags right) noexcept
    -> KeyModifierFlags {
  return static_cast<KeyModifierFlags>(static_cast<UInt32>(left) |
                                       static_cast<UInt32>(right));
}

[[nodiscard]] constexpr auto
HasKeyModifier(const UInt32 modifiers, const KeyModifierFlags flag) noexcept
    -> bool {
  return (modifiers & static_cast<UInt32>(flag)) != 0;
}

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

  [[nodiscard]] constexpr auto Logical() const noexcept -> LogicalKey {
    return static_cast<LogicalKey>(logicalKey);
  }
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
