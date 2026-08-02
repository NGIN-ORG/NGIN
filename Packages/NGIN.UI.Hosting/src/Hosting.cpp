#include <NGIN/UI/Hosting/Hosting.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

struct HostedUIScope::State final {
  struct ScopeRecord final {
    Core::ServiceScopeId id{};
    Core::ServiceScopeKind kind{Core::ServiceScopeKind::Activation};
    Core::ServiceScopeId parent{};
    std::string owner{};
    Window *window{nullptr};
    NGIN::Utilities::Callable<void()> closeHandler{};
    bool closeHandlerPending{false};
    bool closeRequested{false};
  };

  explicit State(NGIN::Memory::Shared<HostedUIRuntime> hostedRuntime)
      : runtime(std::move(hostedRuntime)) {}

  [[nodiscard]] auto Bind(Core::IServiceRegistry &serviceRegistry) noexcept
      -> Core::CoreResult<void> {
    std::scoped_lock lock{mutex};
    if (registry != nullptr) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::InvalidState,
                                "NGIN.UI.Hosting",
                                "HostedUIServiceProvider::Bind",
                                "hosted UI service provider is already bound"));
    }
    auto scope = serviceRegistry.BeginScope(Core::ServiceScopeKind::Application,
                                            "NGIN.UI.Application");
    if (!scope) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(scope.Error());
    }
    registry = &serviceRegistry;
    applicationScope = scope.Value();
    try {
      scopes.emplace(applicationScope.value,
                     ScopeRecord{.id = applicationScope,
                                 .kind = Core::ServiceScopeKind::Application,
                                 .owner = "NGIN.UI.Application"});
    } catch (...) {
      static_cast<void>(serviceRegistry.EndScope(applicationScope));
      registry = nullptr;
      applicationScope = {};
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          HostingError("HostedUIServiceProvider::Bind",
                       "application scope allocation failed"));
    }
    acceptsWork = true;
    return {};
  }

  [[nodiscard]] auto BeginScope(Core::ServiceScopeKind kind, std::string owner,
                                Core::ServiceScopeId parent,
                                Window *window = nullptr) noexcept
      -> Core::CoreResult<Core::ServiceScopeId> {
    std::scoped_lock lock{mutex};
    if (!acceptsWork || registry == nullptr) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::InvalidState,
                                "NGIN.UI.Hosting",
                                "HostedUIServiceProvider::BeginScope",
                                "hosted UI service provider is shutting down"));
    }
    if (!parent.IsGlobal() && !scopes.contains(parent.value)) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::NotFound,
                                "NGIN.UI.Hosting",
                                "HostedUIServiceProvider::BeginScope",
                                "parent UI service scope is not active"));
    }
    auto created = registry->BeginScope(kind, owner);
    if (!created) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(created.Error());
    }
    const auto id = created.Value();
    try {
      scopes.emplace(id.value, ScopeRecord{.id = id,
                                           .kind = kind,
                                           .parent = parent,
                                           .owner = std::move(owner),
                                           .window = window});
      if (window != nullptr) {
        windows[window] = id;
      }
    } catch (...) {
      scopes.erase(id.value);
      static_cast<void>(registry->EndScope(id));
      return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
          "HostedUIServiceProvider::BeginScope", "scope allocation failed"));
    }
    return id;
  }

  [[nodiscard]] auto IsActive(Core::ServiceScopeId id) const noexcept -> bool {
    std::scoped_lock lock{mutex};
    return !id.IsGlobal() && scopes.contains(id.value);
  }

  [[nodiscard]] auto Kind(Core::ServiceScopeId id) const noexcept
      -> Core::ServiceScopeKind {
    std::scoped_lock lock{mutex};
    const auto found = scopes.find(id.value);
    return found != scopes.end() ? found->second.kind
                                 : Core::ServiceScopeKind::Activation;
  }

  [[nodiscard]] auto Provider() const noexcept -> Core::IServiceRegistry * {
    std::scoped_lock lock{mutex};
    return registry;
  }

  [[nodiscard]] auto WindowScope(Window *window) const noexcept
      -> Core::ServiceScopeId {
    std::scoped_lock lock{mutex};
    const auto found = windows.find(window);
    return found != windows.end() ? found->second : Core::ServiceScopeId{};
  }

  [[nodiscard]] auto
  SetCloseHandler(Core::ServiceScopeId id,
                  NGIN::Utilities::Callable<void()> handler) noexcept
      -> Core::CoreResult<void> {
    std::scoped_lock lock{mutex};
    const auto found = scopes.find(id.value);
    if (found == scopes.end()) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::NotFound,
                                "NGIN.UI.Hosting",
                                "HostedUIScope::SetCloseHandler",
                                "hosted UI service scope is not active"));
    }
    if (found->second.closeHandler) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(
              Core::KernelErrorCode::AlreadyExists, "NGIN.UI.Hosting",
              "HostedUIScope::SetCloseHandler",
              "hosted UI service scope already has a lifecycle owner"));
    }
    found->second.closeHandler = std::move(handler);
    return {};
  }

  void ClearCloseHandler(Core::ServiceScopeId id) noexcept {
    std::scoped_lock lock{mutex};
    if (const auto found = scopes.find(id.value); found != scopes.end()) {
      found->second.closeHandler = nullptr;
      found->second.closeHandlerPending = false;
    }
  }

  [[nodiscard]] auto RequestEnd(Core::ServiceScopeId id) noexcept
      -> Core::CoreResult<void> {
    if (id.IsGlobal()) {
      return {};
    }

    std::vector<Core::ServiceScopeId> children;
    try {
      {
        std::scoped_lock lock{mutex};
        const auto found = scopes.find(id.value);
        if (found == scopes.end()) {
          return {};
        }
        found->second.closeRequested = true;
        for (const auto &[_, candidate] : scopes) {
          if (candidate.parent == id) {
            children.push_back(candidate.id);
          }
        }
      }
    } catch (...) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          HostingError("HostedUIServiceProvider::RequestEnd",
                       "scope teardown allocation failed"));
    }
    for (const auto child : children) {
      auto ended = RequestEnd(child);
      if (!ended) {
        return ended;
      }
    }
    return TryFinalize(id);
  }

  [[nodiscard]] auto TryFinalize(Core::ServiceScopeId id) noexcept
      -> Core::CoreResult<void> {
    NGIN::Utilities::Callable<void()> closeHandler;
    Core::ServiceScopeId parent{};
    Window *window = nullptr;
    Core::IServiceRegistry *serviceRegistry = nullptr;
    {
      std::scoped_lock lock{mutex};
      const auto found = scopes.find(id.value);
      if (found == scopes.end() || !found->second.closeRequested) {
        return {};
      }
      const auto hasChildren = std::any_of(
          scopes.begin(), scopes.end(), [id](const auto &candidate) {
            return candidate.second.parent == id;
          });
      if (hasChildren) {
        return {};
      }
      if (found->second.closeHandlerPending) {
        return {};
      }
      if (found->second.closeHandler) {
        closeHandler = std::move(found->second.closeHandler);
        found->second.closeHandlerPending = true;
      } else {
        parent = found->second.parent;
        window = found->second.window;
        serviceRegistry = registry;
        scopes.erase(found);
        if (window != nullptr) {
          windows.erase(window);
        }
        if (applicationScope == id) {
          applicationScope = {};
        }
      }
    }
    if (closeHandler) {
      closeHandler();
      return {};
    }
    if (serviceRegistry != nullptr) {
      auto ended = serviceRegistry->EndScope(id);
      if (!ended) {
        return ended;
      }
    }
    if (!parent.IsGlobal()) {
      return TryFinalize(parent);
    }
    return {};
  }

  void BeginShutdown() noexcept {
    Core::ServiceScopeId application{};
    {
      std::scoped_lock lock{mutex};
      acceptsWork = false;
      application = applicationScope;
    }
    static_cast<void>(RequestEnd(application));
  }

  [[nodiscard]] auto DrainAndShutdown() noexcept -> Core::CoreResult<void> {
    BeginShutdown();
    for (int attempt = 0; attempt < 256; ++attempt) {
      {
        std::scoped_lock lock{mutex};
        if (scopes.empty()) {
          registry = nullptr;
          return {};
        }
      }
      if (!runtime) {
        break;
      }
      auto pumped = runtime->UI().PumpOnce(std::chrono::milliseconds{0});
      if (!pumped) {
        return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
            "HostedUIServiceProvider::DrainAndShutdown", pumped.Error()));
      }
    }
    std::scoped_lock lock{mutex};
    registry = nullptr;
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::InvalidState, "NGIN.UI.Hosting",
        "HostedUIServiceProvider::DrainAndShutdown",
        "hosted UI scopes did not finish cancellation during shutdown"));
  }

  mutable std::mutex mutex{};
  Core::IServiceRegistry *registry{nullptr};
  NGIN::Memory::Shared<HostedUIRuntime> runtime{};
  Core::ServiceScopeId applicationScope{};
  std::unordered_map<UInt64, ScopeRecord> scopes{};
  std::unordered_map<Window *, Core::ServiceScopeId> windows{};
  bool acceptsWork{false};
};

HostedUIScope::HostedUIScope(std::shared_ptr<State> state,
                             const Core::ServiceScopeId id,
                             const Core::ServiceScopeKind kind) noexcept
    : m_state(std::move(state)), m_id(id), m_kind(kind) {}

HostedUIScope::HostedUIScope(HostedUIScope &&other) noexcept
    : m_state(std::move(other.m_state)), m_id(other.m_id),
      m_kind(other.m_kind) {
  other.m_id = {};
}

auto HostedUIScope::operator=(HostedUIScope &&other) noexcept
    -> HostedUIScope & {
  if (this != &other) {
    static_cast<void>(End());
    m_state = std::move(other.m_state);
    m_id = other.m_id;
    m_kind = other.m_kind;
    other.m_id = {};
  }
  return *this;
}

HostedUIScope::~HostedUIScope() { static_cast<void>(End()); }

auto HostedUIScope::Id() const noexcept -> Core::ServiceScopeId { return m_id; }

auto HostedUIScope::Kind() const noexcept -> Core::ServiceScopeKind {
  return m_kind;
}

auto HostedUIScope::IsActive() const noexcept -> bool {
  return m_state && m_state->IsActive(m_id);
}

auto HostedUIScope::End() noexcept -> Core::CoreResult<void> {
  if (!m_state || m_id.IsGlobal()) {
    return {};
  }
  auto ended = m_state->RequestEnd(m_id);
  if (ended && !m_state->IsActive(m_id)) {
    m_id = {};
  }
  return ended;
}

auto HostedUIScope::SetCloseHandler(
    NGIN::Utilities::Callable<void()> handler) noexcept
    -> Core::CoreResult<void> {
  if (!m_state || m_id.IsGlobal() || !handler) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::InvalidArgument, "NGIN.UI.Hosting",
        "HostedUIScope::SetCloseHandler",
        "active scope and close handler are required"));
  }
  return m_state->SetCloseHandler(m_id, std::move(handler));
}

void HostedUIScope::ClearCloseHandler() noexcept {
  if (m_state && !m_id.IsGlobal()) {
    m_state->ClearCloseHandler(m_id);
  }
}

auto HostedUIScope::Provider() const noexcept -> Core::IServiceProvider * {
  return m_state ? m_state->Provider() : nullptr;
}

HostedWindow::HostedWindow(std::shared_ptr<HostedUIScope::State> state,
                           Window *window) noexcept
    : m_state(std::move(state)), m_window(window) {}

auto HostedWindow::UI() const noexcept -> Window * { return m_window; }

auto HostedWindow::ScopeId() const noexcept -> Core::ServiceScopeId {
  return m_state ? m_state->WindowScope(m_window) : Core::ServiceScopeId{};
}

auto HostedWindow::IsOpen() const noexcept -> bool {
  return m_window != nullptr && !m_window->IsClosed() && m_state &&
         m_state->IsActive(ScopeId());
}

auto HostedWindow::CreatePageScope(std::string owner) const noexcept
    -> Core::CoreResult<HostedUIScope> {
  if (!m_state || !IsOpen()) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::InvalidState, "NGIN.UI.Hosting",
        "HostedWindow::CreatePageScope", "hosted UI window is not open"));
  }
  auto created = m_state->BeginScope(Core::ServiceScopeKind::Page,
                                     std::move(owner), ScopeId());
  if (!created) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(created.Error());
  }
  return HostedUIScope{m_state, created.Value(), Core::ServiceScopeKind::Page};
}

auto HostedWindow::Close() noexcept -> Core::CoreResult<void> {
  if (!m_state || m_window == nullptr) {
    return {};
  }
  if (!m_window->IsClosed()) {
    auto closed = m_state->runtime->UI().CloseWindow(*m_window);
    if (!closed) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          HostingError("HostedWindow::Close", closed.Error()));
    }
  }
  return m_state->RequestEnd(ScopeId());
}

auto HostedWindow::Provider() const noexcept -> Core::IServiceProvider * {
  return m_state ? m_state->Provider() : nullptr;
}

HostedUIServiceProvider::HostedUIServiceProvider(
    NGIN::Memory::Shared<HostedUIRuntime> runtime)
    : m_state(std::make_shared<HostedUIScope::State>(std::move(runtime))) {}

HostedUIServiceProvider::~HostedUIServiceProvider() { BeginShutdown(); }

auto HostedUIServiceProvider::Bind(Core::IServiceRegistry &registry) noexcept
    -> Core::CoreResult<void> {
  return m_state->Bind(registry);
}

auto HostedUIServiceProvider::CreateWindow(
    const WindowCreateInfo &info) noexcept -> Core::CoreResult<HostedWindow> {
  if (!m_state || !AcceptsWork()) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::InvalidState, "NGIN.UI.Hosting",
        "HostedUIServiceProvider::CreateWindow",
        "hosted UI service provider is not active"));
  }
  std::string owner;
  try {
    owner = info.id.c_str();
  } catch (...) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(
        HostingError("HostedUIServiceProvider::CreateWindow",
                     "window scope owner allocation failed"));
  }
  auto created = m_state->runtime->UI().CreateWindow(info);
  if (!created) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(
        HostingError("HostedUIServiceProvider::CreateWindow", created.Error()));
  }
  auto scope =
      m_state->BeginScope(Core::ServiceScopeKind::Window, std::move(owner),
                          ApplicationScopeId(), created.Value());
  if (!scope) {
    static_cast<void>(m_state->runtime->UI().CloseWindow(*created.Value()));
    return NGIN::Utilities::Unexpected<Core::KernelError>(scope.Error());
  }
  return HostedWindow{m_state, created.Value()};
}

auto HostedUIServiceProvider::CreatePageScope(Window &window,
                                              std::string owner) noexcept
    -> Core::CoreResult<HostedUIScope> {
  return HostedWindow{m_state, &window}.CreatePageScope(std::move(owner));
}

auto HostedUIServiceProvider::CreateActivationScope(const HostedUIScope &parent,
                                                    std::string owner) noexcept
    -> Core::CoreResult<HostedUIScope> {
  if (!m_state || parent.m_state != m_state || !parent.IsActive()) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(Core::MakeKernelError(
        Core::KernelErrorCode::InvalidArgument, "NGIN.UI.Hosting",
        "HostedUIServiceProvider::CreateActivationScope",
        "activation parent must be an active scope from this provider"));
  }
  auto created = m_state->BeginScope(Core::ServiceScopeKind::Activation,
                                     std::move(owner), parent.Id());
  if (!created) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(created.Error());
  }
  return HostedUIScope{m_state, created.Value(),
                       Core::ServiceScopeKind::Activation};
}

auto HostedUIServiceProvider::CloseWindow(Window &window) noexcept
    -> Core::CoreResult<void> {
  return HostedWindow{m_state, &window}.Close();
}

auto HostedUIServiceProvider::ReconcileClosedWindows() noexcept
    -> Core::CoreResult<void> {
  std::vector<Core::ServiceScopeId> closed;
  try {
    {
      std::scoped_lock lock{m_state->mutex};
      for (const auto &[window, scope] : m_state->windows) {
        if (window == nullptr || window->IsClosed()) {
          closed.push_back(scope);
        }
      }
    }
  } catch (...) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(
        HostingError("HostedUIServiceProvider::ReconcileClosedWindows",
                     "closed-window reconciliation allocation failed"));
  }
  for (const auto scope : closed) {
    auto ended = m_state->RequestEnd(scope);
    if (!ended) {
      return ended;
    }
  }
  return {};
}

void HostedUIServiceProvider::BeginShutdown() noexcept {
  if (m_state) {
    m_state->BeginShutdown();
  }
}

auto HostedUIServiceProvider::DrainAndShutdown() noexcept
    -> Core::CoreResult<void> {
  return m_state ? m_state->DrainAndShutdown() : Core::CoreResult<void>{};
}

auto HostedUIServiceProvider::ApplicationScopeId() const noexcept
    -> Core::ServiceScopeId {
  if (!m_state) {
    return {};
  }
  std::scoped_lock lock{m_state->mutex};
  return m_state->applicationScope;
}

auto HostedUIServiceProvider::ActiveScopeCount() const noexcept -> UIntSize {
  if (!m_state) {
    return 0;
  }
  std::scoped_lock lock{m_state->mutex};
  return m_state->scopes.size();
}

auto HostedUIServiceProvider::AcceptsWork() const noexcept -> bool {
  if (!m_state) {
    return false;
  }
  std::scoped_lock lock{m_state->mutex};
  return m_state->acceptsWork;
}

auto HostedUIServiceProvider::Provider() const noexcept
    -> Core::IServiceProvider * {
  return m_state ? m_state->Provider() : nullptr;
}

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
    NGIN::Memory::Shared<HostedUIServiceProvider> services,
    const std::chrono::milliseconds maximumWait) noexcept
    : m_runtime(std::move(runtime)), m_dispatcher(std::move(dispatcher)),
      m_services(std::move(services)),
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
    if (m_services) {
      auto reconciled = m_services->ReconcileClosedWindows();
      if (!reconciled) {
        return StopWithError(host, reconciled.Error());
      }
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

UIModule::UIModule(
    NGIN::Memory::Shared<HostedUIRuntime> runtime,
    NGIN::Memory::Shared<IUIDispatcher> dispatcher,
    NGIN::Memory::Shared<HostedUIServiceProvider> services) noexcept
    : m_runtime(std::move(runtime)), m_dispatcher(std::move(dispatcher)),
      m_services(std::move(services)) {}

auto UIModule::OnRegister(Core::ModuleContext &context) noexcept
    -> Core::CoreResult<void> {
  if (!m_runtime || !m_dispatcher || !m_services) {
    return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
        "UIModule::OnRegister", "hosted UI runtime services are unavailable"));
  }
  try {
    auto bound = m_services->Bind(context.Services());
    if (!bound) {
      return bound;
    }
    const auto rollbackRegistration = [&] {
      m_services->BeginShutdown();
      static_cast<void>(m_services->DrainAndShutdown());
    };
    auto registered = context.RegisterSingleton<HostedUIRuntime>(
        UIApplicationServiceName, m_runtime);
    if (!registered) {
      rollbackRegistration();
      return registered;
    }
    registered = context.RegisterSingleton<HostedUIServiceProvider>(
        UIServiceProviderServiceName, m_services);
    if (!registered) {
      rollbackRegistration();
      return registered;
    }
    registered = context.RegisterSingleton<HostedUIRuntime>(
        UIWindowManagerServiceName, m_runtime);
    if (!registered) {
      rollbackRegistration();
      return registered;
    }
    registered = context.RegisterSingleton<IUIDispatcher>(
        UIDispatcherServiceName, m_dispatcher);
    if (!registered) {
      rollbackRegistration();
      return registered;
    }
    registered = context.RegisterSingleton<PlatformBackendReference>(
        UIPlatformBackendServiceName,
        NGIN::Memory::MakeShared<PlatformBackendReference>(
            m_runtime->UI().Platform()));
    if (!registered) {
      rollbackRegistration();
      return registered;
    }
    registered = context.RegisterSingleton<RenderBackendReference>(
        UIRenderBackendServiceName,
        NGIN::Memory::MakeShared<RenderBackendReference>(
            m_runtime->UI().Renderer()));
    if (!registered) {
      rollbackRegistration();
    }
    return registered;
  } catch (...) {
    m_services->BeginShutdown();
    static_cast<void>(m_services->DrainAndShutdown());
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
  if (m_services) {
    m_services->BeginShutdown();
  }
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
  if (m_services) {
    auto shutdown = m_services->DrainAndShutdown();
    if (!shutdown) {
      return shutdown;
    }
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
    auto services =
        NGIN::Memory::MakeShared<HostedUIServiceProvider>(runtime.Value());
    auto runLoop = std::make_shared<UIHostRunLoop>(runtime.Value(), dispatcher,
                                                   services, maximumWait);

    Core::ModuleOptions options{};
    options.family = Core::ModuleFamily::Platform;
    options.startupStage = Core::StartupStage::Platform;
    options.providesServices = {
        UIApplicationServiceName,   UIWindowManagerServiceName,
        UIDispatcherServiceName,    UIPlatformBackendServiceName,
        UIRenderBackendServiceName, UIServiceProviderServiceName,
    };
    options.capabilities = {
        Core::ModuleCapability{.name = "UI.PlatformBackend", .exclusive = true},
        Core::ModuleCapability{.name = "UI.RenderBackend", .exclusive = true},
    };

    auto runtimeService = runtime.Value();
    auto dispatcherService = dispatcher;
    auto serviceProvider = services;
    builder.UseRunLoop(runLoop).AddModule(
        UIRuntimeModuleName, std::move(options),
        [runtimeService = std::move(runtimeService),
         dispatcherService = std::move(dispatcherService),
         serviceProvider = std::move(serviceProvider)]() mutable
            -> Core::CoreResult<NGIN::Memory::Shared<Core::IModule>> {
          try {
            return NGIN::Memory::MakeSharedAs<Core::IModule, UIModule>(
                runtimeService, dispatcherService, serviceProvider);
          } catch (...) {
            return NGIN::Utilities::Unexpected<Core::KernelError>(HostingError(
                "UIModule::Create", "UI module allocation failed"));
          }
        });

    return UIHostingRegistration{
        .runtime = runtime.Value(),
        .dispatcher = dispatcher,
        .services = services,
        .runLoop = std::move(runLoop),
    };
  } catch (...) {
    return MakeUIError(UIErrorCode::OutOfMemory,
                       "UI hosting service allocation failed",
                       "NGIN.UI.Hosting", "ConfigureUIHosting");
  }
}
} // namespace NGIN::UI::Hosting
