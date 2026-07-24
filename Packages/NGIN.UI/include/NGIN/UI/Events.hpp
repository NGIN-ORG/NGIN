#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>

#include <type_traits>
#include <variant>
#include <vector>

namespace NGIN::UI {
/// @brief Hardware or synthesized source of a pointer event.
enum class PointerKind : UInt8 {
  Mouse,
  Touch,
  Pen,
};

/// @brief Logical pointer button independent of platform numbering.
enum class PointerButton : UInt8 {
  None,
  Primary,
  Secondary,
  Middle,
  Auxiliary1,
  Auxiliary2,
};

/// @brief Press or release transition of a pointer button.
enum class ButtonState : UInt8 {
  Released,
  Pressed,
};

/// @brief Press, release, or repeat transition of a keyboard key.
enum class KeyState : UInt8 {
  Released,
  Pressed,
  Repeated,
};

/// @brief Modifier keys active for a keyboard or pointer event.
enum class KeyModifierFlags : UInt32 {
  None = 0,
  Shift = 1U << 0U,
  Control = 1U << 1U,
  Alt = 1U << 2U,
  Super = 1U << 3U,
  CapsLock = 1U << 4U,
  NumLock = 1U << 5U,
};

/// @brief Platform-independent meaning assigned to a keyboard key.
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

/// @brief Platform preference for light, dark, or unspecified appearance.
enum class ThemePreference : UInt8 {
  System,
  Light,
  Dark,
  HighContrast,
};

/// @brief Platform request to close a window.
struct WindowCloseRequested final {
  PlatformWindowHandle window{};
};

/// @brief Notification that a window's pixel extent changed.
struct WindowResized final {
  PlatformWindowHandle window{};
  PixelSize size{};
};

/// @brief Notification that a window moved in screen pixels.
struct WindowMoved final {
  PlatformWindowHandle window{};
  PixelPoint position{};
};

/// @brief Notification that a window's device scale factor changed.
struct WindowScaleChanged final {
  PlatformWindowHandle window{};
  F32 scaleFactor{1.0F};
};

/// @brief Notification that a native window gained or lost focus.
struct WindowFocusChanged final {
  PlatformWindowHandle window{};
  bool focused{false};
};

/// @brief Pointer motion expressed in window logical coordinates.
struct PointerMoved final {
  PlatformWindowHandle window{};
  UInt64 pointerId{0};
  PointerKind kind{PointerKind::Mouse};
  Point position{};
  Point delta{};
};

/// @brief Pointer-button transition at a window position.
struct PointerButtonChanged final {
  PlatformWindowHandle window{};
  UInt64 pointerId{0};
  PointerKind kind{PointerKind::Mouse};
  PointerButton button{PointerButton::None};
  ButtonState state{ButtonState::Released};
  Point position{};
};

/// @brief Pointer wheel or trackpad scroll delta.
struct PointerWheelChanged final {
  PlatformWindowHandle window{};
  UInt64 pointerId{0};
  Point delta{};
  Point position{};
};

/// @brief Physical and logical keyboard-key transition.
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

/// @brief Committed text delivered by the platform input method.
struct TextInput final {
  PlatformWindowHandle window{};
  NGIN::Text::String text{};
};

/// @brief Active IME pre-edit text and its selection.
struct TextComposition final {
  PlatformWindowHandle window{};
  NGIN::Text::String text{};
  // Normalized UTF-8 byte offsets within text.
  UIntSize selectionStart{0};
  UIntSize selectionLength{0};
};

/// @brief Files dropped onto a native window.
struct FileDrop final {
  PlatformWindowHandle window{};
  std::vector<NGIN::Text::String> paths{};
  Point position{};
};

/// @brief Notification that the platform theme preference changed.
struct ThemeChanged final {
  ThemePreference preference{ThemePreference::System};
};

/// @brief Variant containing every normalized event emitted by a platform.
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

/// @brief Consumer to which a platform backend publishes normalized events.
class IPlatformEventSink {
public:
  virtual ~IPlatformEventSink() = default;
  virtual void Push(PlatformEvent event) = 0;
};
} // namespace NGIN::UI
