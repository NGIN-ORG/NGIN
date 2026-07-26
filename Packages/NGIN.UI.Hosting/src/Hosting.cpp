#include <NGIN/UI/Hosting/Hosting.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace NGIN::UI::Hosting {
namespace {
[[nodiscard]] auto HostingError(const char *operation, const char *message)
    -> Core::KernelError {
  return Core::MakeKernelError(Core::KernelErrorCode::InternalError,
                               "NGIN.UI.Hosting", operation, message);
}

[[nodiscard]] auto HostingError(const char *operation, const UIError &error)
    -> Core::KernelError {
  std::string message;
  if (!error.backend.Empty()) {
    message += error.backend.CStr();
    message += '/';
  }
  if (!error.operation.Empty()) {
    message += error.operation.CStr();
    message += ": ";
  }
  message += error.message.CStr();
  return Core::MakeKernelError(Core::KernelErrorCode::InternalError,
                               "NGIN.UI.Hosting", operation,
                               std::move(message));
}

[[nodiscard]] auto StopWithError(Core::IApplicationHost &host,
                                 Core::KernelError error) noexcept
    -> Core::CoreResult<void> {
  host.RequestStop(error.message);
  auto shutdown = host.Shutdown();
  if (!shutdown) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(shutdown.Error());
  }
  return NGIN::Utilities::Unexpected<Core::KernelError>(std::move(error));
}
} // namespace

auto HostedUIRuntime::Create(UIHostingCreateInfo info) noexcept
    -> UIResult<NGIN::Memory::Shared<HostedUIRuntime>> {
  auto application = CreateApplication(std::move(info.application));
  if (!application) {
    return std::move(application).Error();
  }
  auto text =
      NativeTextSystem::Create(application.Value()->Renderer(), info.text);
  if (!text) {
    return std::move(text).Error();
  }
  auto *applicationObserver = application.Value().get();
  text.Value()->SetResourcesInvalidatedCallback([applicationObserver] {
    applicationObserver->InvalidateAll(InvalidationKind::All);
  });
  try {
    return NGIN::Memory::MakeShared<HostedUIRuntime>(
        std::move(application).Value(), std::move(text).Value());
  } catch (...) {
    return MakeUIError(UIErrorCode::OutOfMemory,
                       "Hosted UI runtime allocation failed", "NGIN.UI.Hosting",
                       "HostedUIRuntime::Create");
  }
}

HostedUIRuntime::HostedUIRuntime(
    std::unique_ptr<Application> application,
    std::unique_ptr<NativeTextSystem> text) noexcept
    : m_application(std::move(application)), m_text(std::move(text)) {}

auto HostedUIRuntime::UI() noexcept -> Application & { return *m_application; }

auto HostedUIRuntime::Text() noexcept -> NativeTextSystem & { return *m_text; }

PlatformBackendReference::PlatformBackendReference(
    IPlatformBackend &backend) noexcept
    : m_backend(&backend) {}

auto PlatformBackendReference::Get() const noexcept -> IPlatformBackend & {
  return *m_backend;
}

RenderBackendReference::RenderBackendReference(IRenderBackend &backend) noexcept
    : m_backend(&backend) {}

auto RenderBackendReference::Get() const noexcept -> IRenderBackend & {
  return *m_backend;
}

struct UIDispatcher::Impl final {
  explicit Impl(NGIN::Memory::Shared<HostedUIRuntime> hostedRuntime) noexcept
      : runtime(std::move(hostedRuntime)) {}

  NGIN::Memory::Shared<HostedUIRuntime> runtime{};
  std::mutex mutex{};
  std::deque<NGIN::Utilities::Callable<void()>> pending{};
  std::thread::id uiThread{};
};

UIDispatcher::UIDispatcher(NGIN::Memory::Shared<HostedUIRuntime> runtime)
    : m_impl(std::make_unique<Impl>(std::move(runtime))) {}

UIDispatcher::~UIDispatcher() = default;

auto UIDispatcher::Post(NGIN::Utilities::Callable<void()> callback) noexcept
    -> Core::CoreResult<void> {
  if (!callback) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::InvalidArgument, "NGIN.UI.Hosting",
        "UIDispatcher::Post", "dispatcher callback cannot be empty"));
  }
  try {
    {
      std::scoped_lock lock{m_impl->mutex};
      m_impl->pending.push_back(std::move(callback));
    }
    Wake();
    return {};
  } catch (...) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
        "UIDispatcher::Post", "dispatcher queue allocation failed"));
  }
}

auto UIDispatcher::Drain() noexcept -> Core::CoreResult<void> {
  if (!IsCurrentThread()) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::ThreadPolicyViolation, "NGIN.UI.Hosting",
        "UIDispatcher::Drain", "dispatcher must drain on its UI thread"));
  }
  try {
    std::deque<NGIN::Utilities::Callable<void()>> pending;
    {
      std::scoped_lock lock{m_impl->mutex};
      pending.swap(m_impl->pending);
    }
    for (auto &callback : pending) {
      callback();
    }
    return {};
  } catch (...) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
        "UIDispatcher::Drain", "dispatcher callback raised an exception"));
  }
}

void UIDispatcher::Wake() noexcept {
  if (m_impl->runtime) {
    m_impl->runtime->UI().Platform().WakeEventLoop();
  }
}

void UIDispatcher::BindCurrentThread() noexcept {
  m_impl->uiThread = std::this_thread::get_id();
}

auto UIDispatcher::IsCurrentThread() const noexcept -> bool {
  return m_impl->uiThread == std::this_thread::get_id();
}

UIHostRunLoop::UIHostRunLoop(
    NGIN::Memory::Shared<HostedUIRuntime> runtime,
    NGIN::Memory::Shared<IUIDispatcher> dispatcher,
    const std::chrono::milliseconds maximumWait) noexcept
    : m_runtime(std::move(runtime)), m_dispatcher(std::move(dispatcher)),
      m_maximumWait(std::max(maximumWait, std::chrono::milliseconds{0})) {}

auto UIHostRunLoop::Run(Core::IApplicationHost &host) noexcept
    -> Core::CoreResult<void> {
  auto started = host.Start();
  if (!started) {
    return started;
  }
  if (!m_runtime || !m_dispatcher) {
    return StopWithError(
        host, HostingError("UIHostRunLoop::Run",
                           "hosted UI runtime services are unavailable"));
  }

  m_dispatcher->BindCurrentThread();
  bool firstFrame = true;
  while (!host.IsStopRequested() && !m_runtime->UI().ShouldExit()) {
    auto pumped = m_runtime->UI().PumpOnce(
        firstFrame ? std::chrono::milliseconds{0} : m_maximumWait);
    firstFrame = false;
    if (!pumped) {
      return StopWithError(
          host, HostingError("UIHostRunLoop::PumpOnce", pumped.Error()));
    }

    auto dispatched = m_dispatcher->Drain();
    if (!dispatched) {
      return StopWithError(host, dispatched.Error());
    }

    auto ticked = host.Tick();
    if (!ticked) {
      return StopWithError(host, ticked.Error());
    }
  }

  if (!host.IsStopRequested()) {
    host.RequestStop("NGIN.UI application exited");
  }
  auto dispatched = m_dispatcher->Drain();
  if (!dispatched) {
    return StopWithError(host, dispatched.Error());
  }
  return host.Shutdown();
}

void UIHostRunLoop::Wake() noexcept {
  if (m_dispatcher) {
    m_dispatcher->Wake();
  }
}

UIModule::UIModule(NGIN::Memory::Shared<HostedUIRuntime> runtime,
                   NGIN::Memory::Shared<IUIDispatcher> dispatcher) noexcept
    : m_runtime(std::move(runtime)), m_dispatcher(std::move(dispatcher)) {}

auto UIModule::OnRegister(Core::ModuleContext &context) noexcept
    -> Core::CoreResult<void> {
  if (!m_runtime || !m_dispatcher) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
        "UIModule::OnRegister", "hosted UI runtime services are unavailable"));
  }
  try {
    auto registered = context.RegisterSingleton<HostedUIRuntime>(
        UIApplicationServiceName, m_runtime);
    if (!registered) {
      return registered;
    }
    registered = context.RegisterSingleton<HostedUIRuntime>(
        UIWindowManagerServiceName, m_runtime);
    if (!registered) {
      return registered;
    }
    registered = context.RegisterSingleton<IUIDispatcher>(
        UIDispatcherServiceName, m_dispatcher);
    if (!registered) {
      return registered;
    }
    registered = context.RegisterSingleton<PlatformBackendReference>(
        UIPlatformBackendServiceName,
        NGIN::Memory::MakeShared<PlatformBackendReference>(
            m_runtime->UI().Platform()));
    if (!registered) {
      return registered;
    }
    return context.RegisterSingleton<RenderBackendReference>(
        UIRenderBackendServiceName,
        NGIN::Memory::MakeShared<RenderBackendReference>(
            m_runtime->UI().Renderer()));
  } catch (...) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
        "UIModule::OnRegister", "UI service registration allocation failed"));
  }
}

auto UIModule::OnStart(Core::ModuleContext &) noexcept
    -> Core::CoreResult<void> {
  return {};
}

auto UIModule::OnStop(Core::ModuleContext &) noexcept
    -> Core::CoreResult<void> {
  if (m_runtime) {
    m_runtime->UI().RequestExit();
  }
  if (m_dispatcher) {
    m_dispatcher->Wake();
  }
  return {};
}

auto UIModule::OnShutdown(Core::ModuleContext &) noexcept
    -> Core::CoreResult<void> {
  if (!m_runtime) {
    return {};
  }
  auto idle = m_runtime->UI().Renderer().WaitIdle();
  if (!idle) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(
        HostingError("UIModule::OnShutdown", idle.Error()));
  }
  return {};
}

auto ConfigureUIHosting(Core::ApplicationBuilder &builder,
                        UIHostingCreateInfo info) noexcept
    -> UIResult<UIHostingRegistration> {
  const auto maximumWait = info.maximumWait;
  auto runtime = HostedUIRuntime::Create(std::move(info));
  if (!runtime) {
    return std::move(runtime).Error();
  }
  try {
    auto dispatcher = NGIN::Memory::MakeSharedAs<IUIDispatcher, UIDispatcher>(
        runtime.Value());
    auto runLoop = std::make_shared<UIHostRunLoop>(runtime.Value(), dispatcher,
                                                   maximumWait);

    Core::ModuleOptions options{};
    options.family = Core::ModuleFamily::Platform;
    options.startupStage = Core::StartupStage::Platform;
    options.providesServices = {
        UIApplicationServiceName,   UIWindowManagerServiceName,
        UIDispatcherServiceName,    UIPlatformBackendServiceName,
        UIRenderBackendServiceName,
    };
    options.capabilities = {
        Core::ModuleCapability{.name = "UI.PlatformBackend", .exclusive = true},
        Core::ModuleCapability{.name = "UI.RenderBackend", .exclusive = true},
    };

    auto runtimeService = runtime.Value();
    auto dispatcherService = dispatcher;
    builder.UseRunLoop(runLoop).AddModule(
        UIRuntimeModuleName, std::move(options),
        [runtimeService = std::move(runtimeService),
         dispatcherService = std::move(dispatcherService)]() mutable
            -> Core::CoreResult<NGIN::Memory::Shared<Core::IModule>> {
          try {
            return NGIN::Memory::MakeSharedAs<Core::IModule, UIModule>(
                runtimeService, dispatcherService);
          } catch (...) {
            return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
                "UIModule::Create", "UI module allocation failed"));
          }
        });

    return UIHostingRegistration{
        .runtime = runtime.Value(),
        .dispatcher = dispatcher,
        .runLoop = std::move(runLoop),
    };
  } catch (...) {
    return MakeUIError(UIErrorCode::OutOfMemory,
                       "UI hosting service allocation failed",
                       "NGIN.UI.Hosting", "ConfigureUIHosting");
  }
}
} // namespace NGIN::UI::Hosting
