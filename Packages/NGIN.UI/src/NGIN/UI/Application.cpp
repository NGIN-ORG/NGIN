#include <NGIN/UI/Application.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace NGIN::UI {
struct Window::Implementation final {
  Implementation() : reconciler(tree), layoutEngine(tree) {}

  WindowCreateInfo info{};
  PlatformWindowHandle platformHandle{};
  RenderSurfaceHandle surfaceHandle{};
  PixelSize pixelExtent{};
  F32 scaleFactor{1.0F};
  EventHandler eventHandler{};
  Content content{};
  RuntimeTree tree{};
  Reconciler reconciler;
  LayoutEngine layoutEngine;
  UIRenderer uiRenderer{};
  ReconcileStats lastReconcileStats{};
  LayoutPassStats lastLayoutStats{};
  DisplayList displayList{};
  PreparedRenderPacket preparedPacket{};
  bool dirty{true};
  bool compositionDirty{true};
  bool layoutDirty{true};
  bool paintDirty{true};
  bool closeRequested{false};
  bool closed{false};
};

Window::Window(WindowCreateInfo info, const PlatformWindowHandle platformHandle,
               const RenderSurfaceHandle surfaceHandle)
    : m_implementation(std::make_unique<Implementation>()) {
  m_implementation->pixelExtent = info.initialSize;
  m_implementation->info = std::move(info);
  m_implementation->platformHandle = platformHandle;
  m_implementation->surfaceHandle = surfaceHandle;
}

Window::~Window() = default;

auto Window::Id() const noexcept -> const NGIN::Text::String & {
  return m_implementation->info.id;
}

auto Window::PlatformHandle() const noexcept -> PlatformWindowHandle {
  return m_implementation->platformHandle;
}

auto Window::SurfaceHandle() const noexcept -> RenderSurfaceHandle {
  return m_implementation->surfaceHandle;
}

auto Window::PixelExtent() const noexcept -> PixelSize {
  return m_implementation->pixelExtent;
}

auto Window::ScaleFactor() const noexcept -> F32 {
  return m_implementation->scaleFactor;
}

auto Window::IsDirty() const noexcept -> bool {
  return m_implementation->dirty;
}

auto Window::IsClosed() const noexcept -> bool {
  return m_implementation->closed;
}

auto Window::IsCloseRequested() const noexcept -> bool {
  return m_implementation->closeRequested;
}

auto Window::Tree() const noexcept -> const RuntimeTree & {
  return m_implementation->tree;
}

auto Window::LastReconcileStats() const noexcept -> const ReconcileStats & {
  return m_implementation->lastReconcileStats;
}

auto Window::LastLayoutStats() const noexcept -> const LayoutPassStats & {
  return m_implementation->lastLayoutStats;
}

auto Window::DisplayCommandCount() const noexcept -> UIntSize {
  return m_implementation->displayList.size();
}

void Window::SetEventHandler(EventHandler handler) {
  m_implementation->eventHandler = std::move(handler);
}

void Window::SetContent(Content content) {
  m_implementation->content = std::move(content);
  Invalidate(InvalidationKind::All);
}

void Window::Invalidate(const InvalidationKind kind) noexcept {
  if (!m_implementation->closed && kind != InvalidationKind::None) {
    m_implementation->dirty = true;
    if (HasInvalidation(kind, InvalidationKind::Compose)) {
      m_implementation->compositionDirty = true;
    }
    if (HasInvalidation(kind, InvalidationKind::Compose) ||
        HasInvalidation(kind, InvalidationKind::Measure) ||
        HasInvalidation(kind, InvalidationKind::Arrange)) {
      m_implementation->layoutDirty = true;
      m_implementation->paintDirty = true;
    }
    if (HasInvalidation(kind, InvalidationKind::Paint)) {
      m_implementation->paintDirty = true;
    }
  }
}

struct Application::Implementation final {
  class EventCollector final : public IPlatformEventSink {
  public:
    void Push(PlatformEvent event) override {
      events.push_back(std::move(event));
    }

    std::vector<PlatformEvent> events{};
  };

  std::unique_ptr<IPlatformBackend> platform{};
  std::unique_ptr<IRenderBackend> renderer{};
  std::vector<std::unique_ptr<Window>> windows{};
  bool exitRequested{false};

  [[nodiscard]] auto FindWindow(const PlatformWindowHandle handle) noexcept
      -> Window * {
    const auto found =
        std::find_if(windows.begin(), windows.end(),
                     [handle](const std::unique_ptr<Window> &window) {
                       return window->PlatformHandle() == handle;
                     });
    return found == windows.end() ? nullptr : found->get();
  }
};

Application::Application(std::unique_ptr<IPlatformBackend> platform,
                         std::unique_ptr<IRenderBackend> renderer)
    : m_implementation(std::make_unique<Implementation>()) {
  m_implementation->platform = std::move(platform);
  m_implementation->renderer = std::move(renderer);
}

Application::~Application() {
  static_cast<void>(m_implementation->renderer->WaitIdle());
  for (auto &window : m_implementation->windows) {
    if (!window->m_implementation->closed) {
      static_cast<void>(m_implementation->renderer->DestroySurface(
          window->m_implementation->surfaceHandle));
      static_cast<void>(m_implementation->platform->DestroyWindow(
          window->m_implementation->platformHandle));
      window->m_implementation->closed = true;
    }
  }
}

auto Application::CreateWindow(const WindowCreateInfo &info) noexcept
    -> UIResult<Window *> {
  if (info.id.Empty()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Window id must not be empty",
                       m_implementation->platform->Name(), "CreateWindow");
  }
  if (info.initialSize.IsEmpty()) {
    return MakeUIError(
        UIErrorCode::InvalidArgument, "Window initial size must be non-zero",
        m_implementation->platform->Name(), "CreateWindow", info.id.c_str());
  }

  const auto duplicate = std::find_if(
      m_implementation->windows.begin(), m_implementation->windows.end(),
      [&info](const std::unique_ptr<Window> &window) {
        return !window->IsClosed() && window->Id() == info.id;
      });
  if (duplicate != m_implementation->windows.end()) {
    return MakeUIError(
        UIErrorCode::InvalidArgument, "A live window already uses this id",
        m_implementation->platform->Name(), "CreateWindow", info.id.c_str());
  }

  auto platformWindow = m_implementation->platform->CreateWindow(info);
  if (!platformWindow) {
    return std::move(platformWindow).Error();
  }

  auto surface = m_implementation->renderer->CreateSurface(
      platformWindow.Value(), info.initialSize);
  if (!surface) {
    static_cast<void>(
        m_implementation->platform->DestroyWindow(platformWindow.Value()));
    return std::move(surface).Error();
  }

  if (info.initiallyVisible) {
    auto shown = m_implementation->platform->ShowWindow(platformWindow.Value());
    if (!shown) {
      static_cast<void>(
          m_implementation->renderer->DestroySurface(surface.Value()));
      static_cast<void>(
          m_implementation->platform->DestroyWindow(platformWindow.Value()));
      return std::move(shown).Error();
    }
  }

  auto window = std::unique_ptr<Window>(
      new Window{info, platformWindow.Value(), surface.Value()});
  auto *result = window.get();
  m_implementation->windows.push_back(std::move(window));
  return result;
}

auto Application::CloseWindow(Window &window) noexcept -> UIResult<void> {
  if (window.m_implementation->closed) {
    return {};
  }

  auto destroyedSurface = m_implementation->renderer->DestroySurface(
      window.m_implementation->surfaceHandle);
  if (!destroyedSurface) {
    return std::move(destroyedSurface).Error();
  }

  auto destroyedWindow = m_implementation->platform->DestroyWindow(
      window.m_implementation->platformHandle);
  if (!destroyedWindow) {
    return std::move(destroyedWindow).Error();
  }

  window.m_implementation->closed = true;
  window.m_implementation->dirty = false;
  return {};
}

auto Application::PumpOnce(const std::chrono::milliseconds maximumWait) noexcept
    -> UIResult<void> {
  Implementation::EventCollector collector;
  auto eventResult =
      maximumWait.count() > 0
          ? m_implementation->platform->WaitEvents(collector, maximumWait)
          : m_implementation->platform->PollEvents(collector);
  if (!eventResult) {
    return std::move(eventResult).Error();
  }

  for (const auto &event : collector.events) {
    const auto handle = EventWindow(event);
    if (!handle) {
      for (auto &window : m_implementation->windows) {
        if (!window->IsClosed()) {
          window->Invalidate();
          if (window->m_implementation->eventHandler) {
            window->m_implementation->eventHandler(event);
          }
        }
      }
      continue;
    }

    auto *window = m_implementation->FindWindow(handle);
    if (window == nullptr || window->IsClosed()) {
      continue;
    }

    if (window->m_implementation->eventHandler) {
      window->m_implementation->eventHandler(event);
    }

    if (const auto *resized = std::get_if<WindowResized>(&event)) {
      if (!resized->size.IsEmpty()) {
        auto resizedSurface = m_implementation->renderer->ResizeSurface(
            window->m_implementation->surfaceHandle, resized->size);
        if (!resizedSurface) {
          return std::move(resizedSurface).Error();
        }
        window->m_implementation->pixelExtent = resized->size;
      }
      window->Invalidate(InvalidationKind::Measure | InvalidationKind::Arrange |
                         InvalidationKind::Paint);
    } else if (const auto *scale = std::get_if<WindowScaleChanged>(&event)) {
      if (scale->scaleFactor > 0.0F) {
        window->m_implementation->scaleFactor = scale->scaleFactor;
      }
      window->Invalidate(InvalidationKind::Measure | InvalidationKind::Arrange |
                         InvalidationKind::Paint);
    } else if (std::holds_alternative<WindowCloseRequested>(event)) {
      window->m_implementation->closeRequested = true;
    } else {
      window->Invalidate();
    }
  }

  for (auto &window : m_implementation->windows) {
    if (window->IsClosed()) {
      continue;
    }
    if (window->IsCloseRequested()) {
      auto closed = CloseWindow(*window);
      if (!closed) {
        return std::move(closed).Error();
      }
      continue;
    }
    if (!window->IsDirty()) {
      continue;
    }

    if (window->m_implementation->compositionDirty) {
      Composer composer;
      if (window->m_implementation->content) {
        window->m_implementation->content(composer);
      }
      if (!composer.IsBalanced()) {
        return MakeUIError(UIErrorCode::InvalidState,
                           "Composition ended with an open element scope",
                           "NGIN.UI", "Compose", window->Id().c_str());
      }
      window->m_implementation->lastReconcileStats =
          window->m_implementation->reconciler.Reconcile(
              composer.Declarations());
      window->m_implementation->compositionDirty = false;
      window->m_implementation->layoutDirty = true;
      window->m_implementation->paintDirty = true;
    }

    const auto scaleFactor = window->ScaleFactor();
    const Size logicalSize{
        static_cast<F32>(window->PixelExtent().width) / scaleFactor,
        static_cast<F32>(window->PixelExtent().height) / scaleFactor,
    };
    if (window->m_implementation->layoutDirty) {
      window->m_implementation->lastLayoutStats =
          window->m_implementation->layoutEngine.Perform(
              SizeConstraints{.minimum = logicalSize, .maximum = logicalSize},
              Rect{0.0F, 0.0F, logicalSize.width, logicalSize.height});
      window->m_implementation->layoutDirty = false;
      window->m_implementation->paintDirty = true;
    }
    if (window->m_implementation->paintDirty) {
      window->m_implementation->displayList =
          BuildDisplayList(window->m_implementation->tree);
      window->m_implementation->preparedPacket =
          window->m_implementation->uiRenderer.Build(
              window->m_implementation->displayList, window->PixelExtent(),
              scaleFactor);
      window->m_implementation->paintDirty = false;
    }

    auto rendered = m_implementation->renderer->Render(
        window->SurfaceHandle(),
        window->m_implementation->preparedPacket.View());
    if (!rendered) {
      return std::move(rendered).Error();
    }
    auto presented =
        m_implementation->renderer->Present(window->SurfaceHandle());
    if (!presented) {
      return std::move(presented).Error();
    }
    window->m_implementation->dirty = false;
  }

  return {};
}

auto Application::Run() noexcept -> UIResult<void> {
  while (!ShouldExit()) {
    auto pumped = PumpOnce(std::chrono::milliseconds{250});
    if (!pumped) {
      return std::move(pumped).Error();
    }
  }
  return {};
}

void Application::RequestExit() noexcept {
  m_implementation->exitRequested = true;
  m_implementation->platform->WakeEventLoop();
}

auto Application::ShouldExit() const noexcept -> bool {
  return m_implementation->exitRequested || ActiveWindowCount() == 0;
}

auto Application::ActiveWindowCount() const noexcept -> UIntSize {
  return static_cast<UIntSize>(std::count_if(
      m_implementation->windows.begin(), m_implementation->windows.end(),
      [](const std::unique_ptr<Window> &window) {
        return !window->IsClosed();
      }));
}

auto Application::Platform() noexcept -> IPlatformBackend & {
  return *m_implementation->platform;
}

auto Application::Renderer() noexcept -> IRenderBackend & {
  return *m_implementation->renderer;
}

auto CreateApplication(ApplicationCreateInfo info) noexcept
    -> UIResult<std::unique_ptr<Application>> {
  if (!info.platform) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Platform backend is required", "", "CreateApplication");
  }
  if (!info.renderer) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Renderer backend is required", "", "CreateApplication");
  }

  auto platformInitialized = info.platform->Initialize(
      PlatformInitInfo{.applicationName = info.applicationName});
  if (!platformInitialized) {
    return std::move(platformInitialized).Error();
  }

  auto rendererInitialized = info.renderer->Initialize(
      RenderInitInfo{.enableValidation = info.enableRendererValidation});
  if (!rendererInitialized) {
    return std::move(rendererInitialized).Error();
  }

  return std::unique_ptr<Application>(
      new Application{std::move(info.platform), std::move(info.renderer)});
}
} // namespace NGIN::UI
