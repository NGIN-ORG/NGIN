#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <algorithm>
#include <utility>

namespace NGIN::UI::Testing {
auto TestPlatformBackend::Name() const noexcept -> const char * {
  return "TestPlatform";
}

auto TestPlatformBackend::ContractVersion() const noexcept
    -> BackendContractVersion {
  return CurrentBackendContractVersion;
}

auto TestPlatformBackend::Capabilities() const noexcept
    -> PlatformCapabilityFlags {
  return PlatformCapabilityFlags::Clipboard | PlatformCapabilityFlags::IME |
         PlatformCapabilityFlags::MultipleWindows |
         PlatformCapabilityFlags::FileDrop | PlatformCapabilityFlags::PenInput |
         PlatformCapabilityFlags::TouchInput |
         PlatformCapabilityFlags::NativeWindow;
}

auto TestPlatformBackend::Initialize(const PlatformInitInfo &) noexcept
    -> UIResult<void> {
  m_initialized = true;
  return {};
}

auto TestPlatformBackend::CreateWindow(const WindowCreateInfo &info) noexcept
    -> UIResult<PlatformWindowHandle> {
  if (!m_initialized) {
    return MakeUIError(UIErrorCode::BackendUnavailable,
                       "Test platform is not initialized", Name(),
                       "CreateWindow", info.id.c_str());
  }
  if (info.initialSize.IsEmpty()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Window size must be non-zero", Name(), "CreateWindow",
                       info.id.c_str());
  }

  const PlatformWindowHandle handle{m_nextWindowIndex++, 1};
  m_windows.push_back(TestWindowRecord{
      .handle = handle,
      .info = info,
      .bounds =
          PixelRect{0, 0, info.initialSize.width, info.initialSize.height},
  });
  return handle;
}

auto TestPlatformBackend::DestroyWindow(
    const PlatformWindowHandle window) noexcept -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "DestroyWindow");
  }
  record->destroyed = true;
  record->visible = false;
  return {};
}

auto TestPlatformBackend::ShowWindow(const PlatformWindowHandle window) noexcept
    -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "ShowWindow");
  }
  record->visible = true;
  return {};
}

auto TestPlatformBackend::SetWindowTitle(
    const PlatformWindowHandle window, const NGIN::Text::String &title) noexcept
    -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "SetWindowTitle");
  }
  record->info.title = title;
  return {};
}

auto TestPlatformBackend::SetWindowBounds(const PlatformWindowHandle window,
                                          const PixelRect bounds) noexcept
    -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "SetWindowBounds");
  }
  record->bounds = bounds;
  return {};
}

auto TestPlatformBackend::PollEvents(IPlatformEventSink &sink) noexcept
    -> UIResult<void> {
  if (!m_initialized) {
    return MakeUIError(UIErrorCode::BackendUnavailable,
                       "Test platform is not initialized", Name(),
                       "PollEvents");
  }
  DrainEvents(sink);
  return {};
}

auto TestPlatformBackend::WaitEvents(
    IPlatformEventSink &sink,
    const std::chrono::milliseconds maximumWait) noexcept -> UIResult<void> {
  if (!m_initialized) {
    return MakeUIError(UIErrorCode::BackendUnavailable,
                       "Test platform is not initialized", Name(),
                       "WaitEvents");
  }
  if (m_events.empty() && maximumWait.count() > 0) {
    m_now += maximumWait;
  }
  DrainEvents(sink);
  return {};
}

void TestPlatformBackend::WakeEventLoop() noexcept { ++m_wakeCount; }

auto TestPlatformBackend::SetCursor(const PlatformWindowHandle window,
                                    const CursorShape cursor) noexcept
    -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "SetCursor");
  }
  record->cursor = cursor;
  return {};
}

auto TestPlatformBackend::SetClipboardText(
    const NGIN::Text::String &text) noexcept -> UIResult<void> {
  m_clipboard = text;
  return {};
}

auto TestPlatformBackend::GetClipboardText() noexcept
    -> UIResult<NGIN::Text::String> {
  return m_clipboard;
}

auto TestPlatformBackend::StartTextInput(const PlatformWindowHandle window,
                                         const PixelRect candidateRect) noexcept
    -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "StartTextInput");
  }
  record->textInputActive = true;
  record->textInputRect = candidateRect;
  return {};
}

auto TestPlatformBackend::StopTextInput(
    const PlatformWindowHandle window) noexcept -> UIResult<void> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "StopTextInput");
  }
  record->textInputActive = false;
  return {};
}

auto TestPlatformBackend::QueryDisplays() noexcept
    -> UIResult<DisplayInfoList> {
  DisplayInfoList displays;
  displays.push_back(DisplayInfo{
      .id = NGIN::Text::String{"Test.Display.0"},
      .name = NGIN::Text::String{"Deterministic display"},
      .bounds = PixelRect{0, 0, 1920, 1080},
      .workArea = PixelRect{0, 0, 1920, 1040},
      .scaleFactor = 1.0F,
      .primary = true,
  });
  return displays;
}

auto TestPlatformBackend::QueryNativeWindow(
    const PlatformWindowHandle window) noexcept -> UIResult<NativeWindowInfo> {
  auto *record = FindWindow(window);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown platform window",
                       Name(), "QueryNativeWindow");
  }
  return NativeWindowInfo{
      .kind = NativeWindowKind::Win32,
      .value = static_cast<UIntPtr>(window.index) + 1U,
  };
}

void TestPlatformBackend::InjectEvent(PlatformEvent event) {
  m_events.push_back(std::move(event));
}

void TestPlatformBackend::AdvanceTime(
    const std::chrono::milliseconds duration) noexcept {
  m_now += duration;
}

auto TestPlatformBackend::IsInitialized() const noexcept -> bool {
  return m_initialized;
}

auto TestPlatformBackend::Now() const noexcept -> std::chrono::milliseconds {
  return m_now;
}

auto TestPlatformBackend::WakeCount() const noexcept -> UIntSize {
  return m_wakeCount;
}

auto TestPlatformBackend::Windows() const noexcept
    -> const std::vector<TestWindowRecord> & {
  return m_windows;
}

auto TestPlatformBackend::ClipboardText() const noexcept
    -> const NGIN::Text::String & {
  return m_clipboard;
}

auto TestPlatformBackend::FindWindow(const PlatformWindowHandle handle) noexcept
    -> TestWindowRecord * {
  const auto found = std::find_if(m_windows.begin(), m_windows.end(),
                                  [handle](const TestWindowRecord &window) {
                                    return window.handle == handle;
                                  });
  return found == m_windows.end() ? nullptr : &*found;
}

auto TestPlatformBackend::DrainEvents(IPlatformEventSink &sink) -> void {
  while (!m_events.empty()) {
    sink.Push(std::move(m_events.front()));
    m_events.pop_front();
  }
}
} // namespace NGIN::UI::Testing
