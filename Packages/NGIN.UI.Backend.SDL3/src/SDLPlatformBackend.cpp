#include <NGIN/UI/Backend/SDL3/SDL3.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace NGIN::UI::SDL3 {
namespace {
[[nodiscard]] auto SDLError(const UIErrorCode code, const char *operation,
                            const char *fallback) -> UIError {
  const auto *message = SDL_GetError();
  return MakeUIError(
      code, message != nullptr && *message != '\0' ? message : fallback, "SDL3",
      operation);
}

[[nodiscard]] auto Modifiers(const SDL_Keymod modifiers) noexcept -> UInt32 {
  UInt32 result = 0;
  if ((modifiers & SDL_KMOD_SHIFT) != 0) {
    result |= static_cast<UInt32>(KeyModifierFlags::Shift);
  }
  if ((modifiers & SDL_KMOD_CTRL) != 0) {
    result |= static_cast<UInt32>(KeyModifierFlags::Control);
  }
  if ((modifiers & SDL_KMOD_ALT) != 0) {
    result |= static_cast<UInt32>(KeyModifierFlags::Alt);
  }
  if ((modifiers & SDL_KMOD_GUI) != 0) {
    result |= static_cast<UInt32>(KeyModifierFlags::Super);
  }
  if ((modifiers & SDL_KMOD_CAPS) != 0) {
    result |= static_cast<UInt32>(KeyModifierFlags::CapsLock);
  }
  if ((modifiers & SDL_KMOD_NUM) != 0) {
    result |= static_cast<UInt32>(KeyModifierFlags::NumLock);
  }
  return result;
}

[[nodiscard]] auto Logical(const SDL_Keycode key) noexcept -> UInt32 {
  switch (key) {
  case SDLK_BACKSPACE:
    return static_cast<UInt32>(LogicalKey::Backspace);
  case SDLK_TAB:
    return static_cast<UInt32>(LogicalKey::Tab);
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return static_cast<UInt32>(LogicalKey::Enter);
  case SDLK_ESCAPE:
    return static_cast<UInt32>(LogicalKey::Escape);
  case SDLK_SPACE:
    return static_cast<UInt32>(LogicalKey::Space);
  case SDLK_DELETE:
    return static_cast<UInt32>(LogicalKey::Delete);
  case SDLK_HOME:
    return static_cast<UInt32>(LogicalKey::Home);
  case SDLK_END:
    return static_cast<UInt32>(LogicalKey::End);
  case SDLK_LEFT:
    return static_cast<UInt32>(LogicalKey::Left);
  case SDLK_RIGHT:
    return static_cast<UInt32>(LogicalKey::Right);
  case SDLK_UP:
    return static_cast<UInt32>(LogicalKey::Up);
  case SDLK_DOWN:
    return static_cast<UInt32>(LogicalKey::Down);
  default:
    return static_cast<UInt32>(key);
  }
}

[[nodiscard]] auto Button(const UInt8 button) noexcept -> PointerButton {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return PointerButton::Primary;
  case SDL_BUTTON_RIGHT:
    return PointerButton::Secondary;
  case SDL_BUTTON_MIDDLE:
    return PointerButton::Middle;
  case SDL_BUTTON_X1:
    return PointerButton::Auxiliary1;
  case SDL_BUTTON_X2:
    return PointerButton::Auxiliary2;
  default:
    return PointerButton::None;
  }
}

[[nodiscard]] auto ByteOffsetForCharacter(const char *text,
                                          const Int32 character) noexcept
    -> UIntSize {
  if (text == nullptr || character <= 0) {
    return 0;
  }
  UIntSize offset = 0;
  Int32 current = 0;
  while (text[offset] != '\0' && current < character) {
    const auto byte = static_cast<UInt8>(text[offset]);
    offset += byte < 0x80U              ? 1
              : (byte & 0xE0U) == 0xC0U ? 2
              : (byte & 0xF0U) == 0xE0U ? 3
                                        : 4;
    ++current;
  }
  return offset;
}

[[nodiscard]] auto Theme() noexcept -> ThemePreference {
  switch (SDL_GetSystemTheme()) {
  case SDL_SYSTEM_THEME_LIGHT:
    return ThemePreference::Light;
  case SDL_SYSTEM_THEME_DARK:
    return ThemePreference::Dark;
  default:
    return ThemePreference::System;
  }
}

class PlatformBackend final : public IPlatformBackend {
public:
  ~PlatformBackend() override {
    for (auto &[_, record] : m_windows) {
      if (record.window != nullptr) {
        SDL_DestroyWindow(record.window);
      }
    }
    for (auto &[_, cursor] : m_cursors) {
      SDL_DestroyCursor(cursor);
    }
    if (m_initialized) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }
  }

  [[nodiscard]] auto Name() const noexcept -> const char * override {
    return "SDL3";
  }
  [[nodiscard]] auto ContractVersion() const noexcept
      -> BackendContractVersion override {
    return CurrentBackendContractVersion;
  }
  [[nodiscard]] auto Capabilities() const noexcept
      -> PlatformCapabilityFlags override {
    auto capabilities =
        PlatformCapabilityFlags::Clipboard | PlatformCapabilityFlags::IME |
        PlatformCapabilityFlags::MultipleWindows |
        PlatformCapabilityFlags::FileDrop;
#if defined(_WIN32)
    capabilities =
        capabilities | PlatformCapabilityFlags::NativeWindow;
#endif
    return capabilities;
  }

  auto Initialize(const PlatformInitInfo &info) noexcept
      -> UIResult<void> override {
    if (m_initialized) {
      return {};
    }
    if (!info.applicationName.Empty()) {
      SDL_SetAppMetadata(info.applicationName.CStr(), nullptr, nullptr);
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
      return SDLError(UIErrorCode::BackendUnavailable, "Initialize",
                      "SDL initialization failed");
    }
    m_wakeEvent = SDL_RegisterEvents(1);
    if (m_wakeEvent == 0) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
      return SDLError(UIErrorCode::BackendUnavailable, "RegisterWakeEvent",
                      "SDL could not reserve a wake event");
    }
    m_initialized = true;
    return {};
  }

  auto CreateWindow(const WindowCreateInfo &info) noexcept
      -> UIResult<PlatformWindowHandle> override {
    if (!m_initialized || info.initialSize.IsEmpty() ||
        info.initialSize.width >
            static_cast<UInt32>(std::numeric_limits<int>::max()) ||
        info.initialSize.height >
            static_cast<UInt32>(std::numeric_limits<int>::max())) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "SDL3 requires initialization and a valid window size",
                         Name(), "CreateWindow");
    }
    auto flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (info.resizable) {
      flags |= SDL_WINDOW_RESIZABLE;
    }
    auto *window = SDL_CreateWindow(
        info.title.CStr(), static_cast<int>(info.initialSize.width),
        static_cast<int>(info.initialSize.height), flags);
    if (window == nullptr) {
      return SDLError(UIErrorCode::WindowCreationFailed, "CreateWindow",
                      "SDL window creation failed");
    }
    if (info.minimumSize.width > 0 && info.minimumSize.height > 0) {
      static_cast<void>(SDL_SetWindowMinimumSize(
          window, static_cast<int>(info.minimumSize.width),
          static_cast<int>(info.minimumSize.height)));
    }
    if (info.owner) {
      auto *owner = FindWindow(info.owner);
      if (owner == nullptr || !SDL_SetWindowParent(window, owner) ||
          (info.modal && !SDL_SetWindowModal(window, true))) {
        SDL_DestroyWindow(window);
        return SDLError(UIErrorCode::WindowCreationFailed, "CreateDialog",
                        "SDL dialog ownership failed");
      }
    }
    const auto id = SDL_GetWindowID(window);
    PlatformWindowHandle handle{};
    try {
      auto &record = m_windows[id];
      record.generation = record.generation == 0 ? 1 : record.generation + 1;
      record.window = window;
      handle = PlatformWindowHandle{id, record.generation};
    } catch (...) {
      SDL_DestroyWindow(window);
      return MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL window tracking allocation failed", Name(),
                         "CreateWindow");
    }
    if (info.initiallyVisible && !SDL_ShowWindow(window)) {
      SDL_DestroyWindow(window);
      m_windows[id].window = nullptr;
      return SDLError(UIErrorCode::WindowCreationFailed, "ShowWindow",
                      "SDL could not show the created window");
    }
    return handle;
  }

  auto DestroyWindow(const PlatformWindowHandle handle) noexcept
      -> UIResult<void> override {
    auto *window = FindWindow(handle);
    if (window == nullptr) {
      return MakeUIError(UIErrorCode::InvalidArgument, "Unknown SDL window",
                         Name(), "DestroyWindow");
    }
    SDL_DestroyWindow(window);
    m_windows[handle.index].window = nullptr;
    return {};
  }

  auto ShowWindow(const PlatformWindowHandle handle) noexcept
      -> UIResult<void> override {
    auto *window = FindWindow(handle);
    if (window == nullptr || !SDL_ShowWindow(window)) {
      return SDLError(UIErrorCode::InvalidArgument, "ShowWindow",
                      "Unknown SDL window");
    }
    return {};
  }

  auto SetWindowTitle(const PlatformWindowHandle handle,
                      const NGIN::Text::String &title) noexcept
      -> UIResult<void> override {
    auto *window = FindWindow(handle);
    if (window == nullptr || !SDL_SetWindowTitle(window, title.CStr())) {
      return SDLError(UIErrorCode::InvalidArgument, "SetWindowTitle",
                      "Unknown SDL window");
    }
    return {};
  }

  auto SetWindowBounds(const PlatformWindowHandle handle,
                       const PixelRect bounds) noexcept
      -> UIResult<void> override {
    auto *window = FindWindow(handle);
    if (window == nullptr || bounds.width == 0 || bounds.height == 0 ||
        bounds.width > static_cast<UInt32>(std::numeric_limits<int>::max()) ||
        bounds.height > static_cast<UInt32>(std::numeric_limits<int>::max()) ||
        !SDL_SetWindowPosition(window, bounds.x, bounds.y) ||
        !SDL_SetWindowSize(window, static_cast<int>(bounds.width),
                           static_cast<int>(bounds.height))) {
      return SDLError(UIErrorCode::InvalidArgument, "SetWindowBounds",
                      "Invalid SDL window bounds");
    }
    return {};
  }

  auto PollEvents(IPlatformEventSink &sink) noexcept
      -> UIResult<void> override {
    try {
      SDL_Event event{};
      while (SDL_PollEvent(&event)) {
        Translate(event, sink);
      }
      return {};
    } catch (...) {
      return MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL event translation allocation failed", Name(),
                         "PollEvents");
    }
  }

  auto WaitEvents(IPlatformEventSink &sink,
                  const std::chrono::milliseconds maximumWait) noexcept
      -> UIResult<void> override {
    if (maximumWait.count() <= 0) {
      return PollEvents(sink);
    }
    try {
      SDL_Event event{};
      SDL_ClearError();
      const auto timeout = static_cast<Sint32>(std::min<Int64>(
          maximumWait.count(), std::numeric_limits<Sint32>::max()));
      if (SDL_WaitEventTimeout(&event, timeout)) {
        Translate(event, sink);
      } else if (const auto *error = SDL_GetError();
                 error != nullptr && *error != '\0') {
        return SDLError(UIErrorCode::BackendUnavailable, "WaitEvents",
                        "SDL event wait failed");
      }
      return PollEvents(sink);
    } catch (...) {
      return MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL event translation allocation failed", Name(),
                         "WaitEvents");
    }
  }

  void WakeEventLoop() noexcept override {
    if (m_initialized) {
      SDL_Event event{};
      event.type = m_wakeEvent;
      static_cast<void>(SDL_PushEvent(&event));
    }
  }

  auto SetCursor(const PlatformWindowHandle handle,
                 const CursorShape shape) noexcept -> UIResult<void> override {
    if (FindWindow(handle) == nullptr) {
      return MakeUIError(UIErrorCode::InvalidArgument, "Unknown SDL window",
                         Name(), "SetCursor");
    }
    if (shape == CursorShape::Hidden) {
      static_cast<void>(SDL_HideCursor());
      return {};
    }
    static_cast<void>(SDL_ShowCursor());
    auto found = m_cursors.find(shape);
    if (found == m_cursors.end()) {
      auto *cursor = SDL_CreateSystemCursor(SystemCursor(shape));
      if (cursor == nullptr) {
        return SDLError(UIErrorCode::ResourceFailed, "CreateCursor",
                        "SDL cursor creation failed");
      }
      try {
        found = m_cursors.emplace(shape, cursor).first;
      } catch (...) {
        SDL_DestroyCursor(cursor);
        return MakeUIError(UIErrorCode::OutOfMemory,
                           "SDL cursor cache allocation failed", Name(),
                           "SetCursor");
      }
    }
    if (!SDL_SetCursor(found->second)) {
      return SDLError(UIErrorCode::ResourceFailed, "SetCursor",
                      "SDL cursor update failed");
    }
    return {};
  }

  auto SetClipboardText(const NGIN::Text::String &text) noexcept
      -> UIResult<void> override {
    if (!SDL_SetClipboardText(text.CStr())) {
      return SDLError(UIErrorCode::BackendUnavailable, "SetClipboardText",
                      "SDL clipboard update failed");
    }
    return {};
  }

  auto GetClipboardText() noexcept -> UIResult<NGIN::Text::String> override {
    auto *text = SDL_GetClipboardText();
    if (text == nullptr) {
      return SDLError(UIErrorCode::BackendUnavailable, "GetClipboardText",
                      "SDL clipboard read failed");
    }
    try {
      NGIN::Text::String result{text};
      SDL_free(text);
      return result;
    } catch (...) {
      SDL_free(text);
      return MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL clipboard text allocation failed", Name(),
                         "GetClipboardText");
    }
  }

  auto StartTextInput(const PlatformWindowHandle handle,
                      const PixelRect candidateRect) noexcept
      -> UIResult<void> override {
    auto *window = FindWindow(handle);
    if (window == nullptr ||
        candidateRect.width >
            static_cast<UInt32>(std::numeric_limits<int>::max()) ||
        candidateRect.height >
            static_cast<UInt32>(std::numeric_limits<int>::max())) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Invalid SDL text input target", Name(),
                         "StartTextInput");
    }
    const SDL_Rect area{candidateRect.x, candidateRect.y,
                        static_cast<int>(candidateRect.width),
                        static_cast<int>(candidateRect.height)};
    if (!SDL_SetTextInputArea(window, &area, 0) ||
        !SDL_StartTextInput(window)) {
      return SDLError(UIErrorCode::BackendUnavailable, "StartTextInput",
                      "SDL text input activation failed");
    }
    return {};
  }

  auto StopTextInput(const PlatformWindowHandle handle) noexcept
      -> UIResult<void> override {
    auto *window = FindWindow(handle);
    if (window == nullptr || !SDL_StopTextInput(window)) {
      return SDLError(UIErrorCode::BackendUnavailable, "StopTextInput",
                      "SDL text input deactivation failed");
    }
    return {};
  }

  auto QueryDisplays() noexcept -> UIResult<DisplayInfoList> override {
    try {
      int count = 0;
      auto *displays = SDL_GetDisplays(&count);
      if (displays == nullptr) {
        return SDLError(UIErrorCode::BackendUnavailable, "QueryDisplays",
                        "SDL display enumeration failed");
      }
      DisplayInfoList result;
      result.reserve(static_cast<UIntSize>(std::max(0, count)));
      const auto primary = SDL_GetPrimaryDisplay();
      for (int index = 0; index < count; ++index) {
        SDL_Rect bounds{};
        SDL_Rect work{};
        if (!SDL_GetDisplayBounds(displays[index], &bounds) ||
            !SDL_GetDisplayUsableBounds(displays[index], &work)) {
          SDL_free(displays);
          return SDLError(UIErrorCode::BackendUnavailable, "QueryDisplays",
                          "SDL display bounds query failed");
        }
        const auto id = std::to_string(displays[index]);
        const auto *name = SDL_GetDisplayName(displays[index]);
        result.push_back(DisplayInfo{
            .id = NGIN::Text::String{id},
            .name = NGIN::Text::String{name != nullptr ? name : id.c_str()},
            .bounds =
                PixelRect{bounds.x, bounds.y, static_cast<UInt32>(bounds.w),
                          static_cast<UInt32>(bounds.h)},
            .workArea = PixelRect{work.x, work.y, static_cast<UInt32>(work.w),
                                  static_cast<UInt32>(work.h)},
            .scaleFactor = SDL_GetDisplayContentScale(displays[index]),
            .primary = displays[index] == primary,
        });
      }
      SDL_free(displays);
      return result;
    } catch (...) {
      return MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL display enumeration allocation failed", Name(),
                         "QueryDisplays");
    }
  }

  auto QueryNativeWindow(const PlatformWindowHandle handle) noexcept
      -> UIResult<NativeWindowInfo> override {
    auto *window = FindWindow(handle);
    if (window == nullptr) {
      return MakeUIError(UIErrorCode::InvalidArgument, "Unknown SDL window",
                         Name(), "QueryNativeWindow");
    }
#if defined(_WIN32)
    const auto properties = SDL_GetWindowProperties(window);
    auto *native = SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (native == nullptr) {
      return SDLError(UIErrorCode::BackendUnavailable, "QueryNativeWindow",
                      "SDL did not expose the Win32 window handle");
    }
    return NativeWindowInfo{
        .kind = NativeWindowKind::Win32,
        .value = reinterpret_cast<UIntPtr>(native),
    };
#else
    return MakeUIError(
        UIErrorCode::Unsupported,
        "This SDL3 build has no native accessibility window provider", Name(),
        "QueryNativeWindow");
#endif
  }

private:
  struct WindowRecord final {
    SDL_Window *window{nullptr};
    UInt32 generation{0};
  };

  [[nodiscard]] auto FindWindow(const PlatformWindowHandle handle) noexcept
      -> SDL_Window * {
    const auto found = m_windows.find(handle.index);
    return found != m_windows.end() &&
                   found->second.generation == handle.generation
               ? found->second.window
               : nullptr;
  }

  [[nodiscard]] auto Handle(const SDL_WindowID id) const noexcept
      -> PlatformWindowHandle {
    const auto found = m_windows.find(id);
    return found != m_windows.end() && found->second.window != nullptr
               ? PlatformWindowHandle{id, found->second.generation}
               : PlatformWindowHandle{};
  }

  static auto SystemCursor(const CursorShape shape) noexcept
      -> SDL_SystemCursor {
    switch (shape) {
    case CursorShape::Text:
      return SDL_SYSTEM_CURSOR_TEXT;
    case CursorShape::Pointer:
      return SDL_SYSTEM_CURSOR_POINTER;
    case CursorShape::Crosshair:
      return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case CursorShape::ResizeHorizontal:
      return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case CursorShape::ResizeVertical:
      return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case CursorShape::ResizeDiagonalNorthWestSouthEast:
      return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case CursorShape::ResizeDiagonalNorthEastSouthWest:
      return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    default:
      return SDL_SYSTEM_CURSOR_DEFAULT;
    }
  }

  void Translate(const SDL_Event &event, IPlatformEventSink &sink) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      sink.Push(WindowCloseRequested{Handle(event.window.windowID)});
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      sink.Push(WindowResized{
          Handle(event.window.windowID),
          PixelSize{static_cast<UInt32>(event.window.data1),
                    static_cast<UInt32>(event.window.data2)},
      });
      break;
    case SDL_EVENT_WINDOW_MOVED:
      sink.Push(
          WindowMoved{Handle(event.window.windowID),
                      PixelPoint{event.window.data1, event.window.data2}});
      break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
      auto *window = SDL_GetWindowFromID(event.window.windowID);
      sink.Push(WindowScaleChanged{
          Handle(event.window.windowID),
          window != nullptr ? SDL_GetWindowDisplayScale(window) : 1.0F});
      break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      sink.Push(
          WindowFocusChanged{Handle(event.window.windowID),
                             event.type == SDL_EVENT_WINDOW_FOCUS_GAINED});
      break;
    case SDL_EVENT_MOUSE_MOTION:
      sink.Push(PointerMoved{
          Handle(event.motion.windowID),
          static_cast<UInt64>(event.motion.which),
          PointerKind::Mouse,
          Point{event.motion.x, event.motion.y},
          Point{event.motion.xrel, event.motion.yrel},
      });
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      sink.Push(PointerButtonChanged{
          Handle(event.button.windowID),
          static_cast<UInt64>(event.button.which),
          PointerKind::Mouse,
          Button(event.button.button),
          event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? ButtonState::Pressed
                                                    : ButtonState::Released,
          Point{event.button.x, event.button.y},
      });
      break;
    case SDL_EVENT_MOUSE_WHEEL: {
      const auto direction =
          event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
      sink.Push(PointerWheelChanged{
          Handle(event.wheel.windowID),
          static_cast<UInt64>(event.wheel.which),
          Point{event.wheel.x * direction, event.wheel.y * direction},
          Point{event.wheel.mouse_x, event.wheel.mouse_y},
      });
      break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      sink.Push(KeyChanged{
          Handle(event.key.windowID),
          static_cast<UInt32>(event.key.scancode),
          Logical(event.key.key),
          event.type == SDL_EVENT_KEY_UP
              ? KeyState::Released
              : (event.key.repeat ? KeyState::Repeated : KeyState::Pressed),
          Modifiers(event.key.mod),
      });
      break;
    case SDL_EVENT_TEXT_INPUT:
      if (event.text.text != nullptr) {
        sink.Push(TextInput{Handle(event.text.windowID),
                            NGIN::Text::String{event.text.text}});
      }
      break;
    case SDL_EVENT_TEXT_EDITING: {
      if (event.edit.text == nullptr) {
        break;
      }
      const auto start =
          ByteOffsetForCharacter(event.edit.text, event.edit.start);
      const auto end = ByteOffsetForCharacter(
          event.edit.text, event.edit.start + event.edit.length);
      sink.Push(TextComposition{
          Handle(event.edit.windowID),
          NGIN::Text::String{event.edit.text},
          start,
          end - start,
      });
      break;
    }
    case SDL_EVENT_DROP_FILE:
      if (event.drop.data != nullptr) {
        sink.Push(FileDrop{
            Handle(event.drop.windowID),
            {NGIN::Text::String{event.drop.data}},
            Point{event.drop.x, event.drop.y},
        });
      }
      break;
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
      sink.Push(ThemeChanged{Theme()});
      break;
    default:
      break;
    }
  }

  bool m_initialized{false};
  UInt32 m_wakeEvent{0};
  std::unordered_map<SDL_WindowID, WindowRecord> m_windows{};
  std::unordered_map<CursorShape, SDL_Cursor *> m_cursors{};
};
} // namespace

auto CreatePlatformBackend() noexcept -> std::unique_ptr<IPlatformBackend> {
  try {
    return std::make_unique<PlatformBackend>();
  } catch (...) {
    return {};
  }
}
} // namespace NGIN::UI::SDL3
