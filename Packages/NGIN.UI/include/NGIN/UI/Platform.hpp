#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Backend.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Events.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>

#include <chrono>
#include <vector>

namespace NGIN::UI {
/// @brief Standard pointer cursor requested by the current hit target.
enum class CursorShape : UInt8 {
  Arrow,
  Text,
  Pointer,
  Crosshair,
  ResizeHorizontal,
  ResizeVertical,
  ResizeDiagonalNorthWestSouthEast,
  ResizeDiagonalNorthEastSouthWest,
  Hidden,
};

/// @brief Application identity passed when initializing a platform backend.
struct PlatformInitInfo final {
  NGIN::Text::String applicationName{};
};

/// @brief Native role of a top-level or dialog window.
enum class WindowKind : UInt8 {
  TopLevel,
  Dialog,
};

/// @brief Identity, title, initial extent, and role of a new window.
struct WindowCreateInfo final {
  NGIN::Text::String id{};
  NGIN::Text::String title{};
  PixelSize initialSize{1280, 720};
  PixelSize minimumSize{};
  bool resizable{true};
  bool initiallyVisible{true};
  WindowKind kind{WindowKind::TopLevel};
  PlatformWindowHandle owner{};
  bool modal{false};
};

/// @brief Logical and pixel geometry plus scale for a physical display.
struct DisplayInfo final {
  NGIN::Text::String id{};
  NGIN::Text::String name{};
  PixelRect bounds{};
  PixelRect workArea{};
  F32 scaleFactor{1.0F};
  bool primary{false};
};

/// @brief Owned list returned when enumerating physical displays.
using DisplayInfoList = std::vector<DisplayInfo>;

/// @brief Operating-system family of an optional native window handle.
enum class NativeWindowKind : UInt8 {
  None,
  Win32,
  Cocoa,
  X11,
  Wayland,
};

/// @brief Opaque native window identity exposed only to platform providers.
struct NativeWindowInfo final {
  NativeWindowKind kind{NativeWindowKind::None};
  UIntPtr value{0};

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
    return kind != NativeWindowKind::None && value != 0;
  }
};

/// @brief Native windowing, input, clipboard, IME, and dispatcher contract.
class IPlatformBackend {
public:
  virtual ~IPlatformBackend() = default;

  [[nodiscard]] virtual auto Name() const noexcept -> const char * = 0;
  [[nodiscard]] virtual auto ContractVersion() const noexcept
      -> BackendContractVersion = 0;
  [[nodiscard]] virtual auto Capabilities() const noexcept
      -> PlatformCapabilityFlags = 0;
  virtual auto Initialize(const PlatformInitInfo &info) noexcept
      -> UIResult<void> = 0;
  virtual auto CreateWindow(const WindowCreateInfo &info) noexcept
      -> UIResult<PlatformWindowHandle> = 0;
  virtual auto DestroyWindow(PlatformWindowHandle window) noexcept
      -> UIResult<void> = 0;
  virtual auto ShowWindow(PlatformWindowHandle window) noexcept
      -> UIResult<void> = 0;
  virtual auto SetWindowTitle(PlatformWindowHandle window,
                              const NGIN::Text::String &title) noexcept
      -> UIResult<void> = 0;
  virtual auto SetWindowBounds(PlatformWindowHandle window,
                               PixelRect bounds) noexcept -> UIResult<void> = 0;
  virtual auto PollEvents(IPlatformEventSink &sink) noexcept
      -> UIResult<void> = 0;
  virtual auto WaitEvents(IPlatformEventSink &sink,
                          std::chrono::milliseconds maximumWait) noexcept
      -> UIResult<void> = 0;
  virtual void WakeEventLoop() noexcept = 0;
  virtual auto SetCursor(PlatformWindowHandle window,
                         CursorShape cursor) noexcept -> UIResult<void> = 0;
  virtual auto SetClipboardText(const NGIN::Text::String &text) noexcept
      -> UIResult<void> = 0;
  virtual auto GetClipboardText() noexcept -> UIResult<NGIN::Text::String> = 0;
  virtual auto StartTextInput(PlatformWindowHandle window,
                              PixelRect candidateRect) noexcept
      -> UIResult<void> = 0;
  virtual auto StopTextInput(PlatformWindowHandle window) noexcept
      -> UIResult<void> = 0;
  virtual auto QueryDisplays() noexcept -> UIResult<DisplayInfoList> = 0;
  [[nodiscard]] virtual auto
  QueryNativeWindow(PlatformWindowHandle) noexcept
      -> UIResult<NativeWindowInfo> {
    return MakeUIError(UIErrorCode::Unsupported,
                       "The platform backend does not expose native windows",
                       Name(), "QueryNativeWindow");
  }
};
} // namespace NGIN::UI
