#pragma once

#include <NGIN/UI/Platform.hpp>

#include <chrono>
#include <deque>
#include <vector>

namespace NGIN::UI::Testing {
/// @brief Native-window state retained by the deterministic test platform.
struct TestWindowRecord final {
  PlatformWindowHandle handle{};
  WindowCreateInfo info{};
  PixelRect bounds{};
  CursorShape cursor{CursorShape::Arrow};
  PixelRect textInputRect{};
  bool visible{false};
  bool textInputActive{false};
  bool destroyed{false};
};

/// @brief In-memory platform backend with explicit event injection for tests.
class TestPlatformBackend : public IPlatformBackend {
public:
  [[nodiscard]] auto Name() const noexcept -> const char * override;
  [[nodiscard]] auto ContractVersion() const noexcept
      -> BackendContractVersion override;
  [[nodiscard]] auto Capabilities() const noexcept
      -> PlatformCapabilityFlags override;
  auto Initialize(const PlatformInitInfo &info) noexcept
      -> UIResult<void> override;
  auto CreateWindow(const WindowCreateInfo &info) noexcept
      -> UIResult<PlatformWindowHandle> override;
  auto DestroyWindow(PlatformWindowHandle window) noexcept
      -> UIResult<void> override;
  auto ShowWindow(PlatformWindowHandle window) noexcept
      -> UIResult<void> override;
  auto SetWindowTitle(PlatformWindowHandle window,
                      const NGIN::Text::String &title) noexcept
      -> UIResult<void> override;
  auto SetWindowBounds(PlatformWindowHandle window, PixelRect bounds) noexcept
      -> UIResult<void> override;
  auto PollEvents(IPlatformEventSink &sink) noexcept -> UIResult<void> override;
  auto WaitEvents(IPlatformEventSink &sink,
                  std::chrono::milliseconds maximumWait) noexcept
      -> UIResult<void> override;
  void WakeEventLoop() noexcept override;
  auto SetCursor(PlatformWindowHandle window, CursorShape cursor) noexcept
      -> UIResult<void> override;
  auto SetClipboardText(const NGIN::Text::String &text) noexcept
      -> UIResult<void> override;
  auto GetClipboardText() noexcept -> UIResult<NGIN::Text::String> override;
  auto StartTextInput(PlatformWindowHandle window,
                      PixelRect candidateRect) noexcept
      -> UIResult<void> override;
  auto StopTextInput(PlatformWindowHandle window) noexcept
      -> UIResult<void> override;
  auto QueryDisplays() noexcept -> UIResult<DisplayInfoList> override;
  auto QueryNativeWindow(PlatformWindowHandle window) noexcept
      -> UIResult<NativeWindowInfo> override;

  void InjectEvent(PlatformEvent event);
  void AdvanceTime(std::chrono::milliseconds duration) noexcept;

  [[nodiscard]] auto IsInitialized() const noexcept -> bool;
  [[nodiscard]] auto Now() const noexcept -> std::chrono::milliseconds;
  [[nodiscard]] auto WakeCount() const noexcept -> UIntSize;
  [[nodiscard]] auto Windows() const noexcept
      -> const std::vector<TestWindowRecord> &;
  [[nodiscard]] auto ClipboardText() const noexcept
      -> const NGIN::Text::String &;

private:
  [[nodiscard]] auto FindWindow(PlatformWindowHandle handle) noexcept
      -> TestWindowRecord *;
  auto DrainEvents(IPlatformEventSink &sink) -> void;

  bool m_initialized{false};
  UInt32 m_nextWindowIndex{0};
  UIntSize m_wakeCount{0};
  std::chrono::milliseconds m_now{0};
  NGIN::Text::String m_clipboard{};
  std::vector<TestWindowRecord> m_windows{};
  std::deque<PlatformEvent> m_events{};
};
} // namespace NGIN::UI::Testing
