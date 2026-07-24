#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Events.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>

#include <chrono>
#include <vector>

namespace NGIN::UI {
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

struct PlatformInitInfo final {
  NGIN::Text::String applicationName{};
};

struct WindowCreateInfo final {
  NGIN::Text::String id{};
  NGIN::Text::String title{};
  PixelSize initialSize{1280, 720};
  PixelSize minimumSize{};
  bool resizable{true};
  bool initiallyVisible{true};
};

struct DisplayInfo final {
  NGIN::Text::String id{};
  NGIN::Text::String name{};
  PixelRect bounds{};
  PixelRect workArea{};
  F32 scaleFactor{1.0F};
  bool primary{false};
};

using DisplayList = std::vector<DisplayInfo>;

class IPlatformBackend {
public:
  virtual ~IPlatformBackend() = default;

  [[nodiscard]] virtual auto Name() const noexcept -> const char * = 0;
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
  virtual auto QueryDisplays() noexcept -> UIResult<DisplayList> = 0;
};
} // namespace NGIN::UI
