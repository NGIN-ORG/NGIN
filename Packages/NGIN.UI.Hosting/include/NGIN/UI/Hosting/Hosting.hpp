#pragma once

#include <NGIN/Core/Core.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/UI/UI.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace NGIN::UI::Hosting {
inline constexpr auto UIApplicationServiceName = "NGIN.UI.IApplication";
inline constexpr auto UIWindowManagerServiceName = "NGIN.UI.IWindowManager";
inline constexpr auto UIDispatcherServiceName = "NGIN.UI.IUIDispatcher";
inline constexpr auto UIPlatformBackendServiceName = "NGIN.UI.IPlatformBackend";
inline constexpr auto UIRenderBackendServiceName = "NGIN.UI.IRenderBackend";
inline constexpr auto UIServiceProviderServiceName =
    "NGIN.UI.Hosting.IServiceProvider";
inline constexpr auto UIRuntimeModuleName = "NGIN.UI.Runtime";

/// @brief Forward declaration for the hosted runtime owner.
class HostedUIRuntime;
/// @brief Forward declaration for the hosted Core service adapter.
class HostedUIServiceProvider;
/// @brief Forward declaration for the hosted ViewModel lifetime owner.
template <typename T> class HostedViewModelHost;

/// @brief Move-only Core service scope owned by one hosted UI lifetime.
class HostedUIScope final {
public:
  HostedUIScope() noexcept = default;
  HostedUIScope(const HostedUIScope &) = delete;
  HostedUIScope(HostedUIScope &&other) noexcept;
  auto operator=(const HostedUIScope &) -> HostedUIScope & = delete;
  auto operator=(HostedUIScope &&other) noexcept -> HostedUIScope &;
  ~HostedUIScope();

  [[nodiscard]] auto Id() const noexcept -> Core::ServiceScopeId;
  [[nodiscard]] auto Kind() const noexcept -> Core::ServiceScopeKind;
  [[nodiscard]] auto IsActive() const noexcept -> bool;
  auto End() noexcept -> Core::CoreResult<void>;

  template <typename T>
  [[nodiscard]] auto ResolveRequired(std::string_view name = {}) const noexcept
      -> Core::CoreResult<NGIN::Memory::Shared<std::remove_cvref_t<T>>> {
    auto *provider = Provider();
    if (provider == nullptr || !IsActive()) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::InvalidState,
                                "NGIN.UI.Hosting",
                                "HostedUIScope::ResolveRequired",
                                "hosted UI service scope is not active"));
    }
    return provider->ResolveRequired<T>(name, Id());
  }

private:
  struct State;
  HostedUIScope(std::shared_ptr<State> state, Core::ServiceScopeId id,
                Core::ServiceScopeKind kind) noexcept;
  [[nodiscard]] auto Provider() const noexcept -> Core::IServiceProvider *;
  auto SetCloseHandler(NGIN::Utilities::Callable<void()> handler) noexcept
      -> Core::CoreResult<void>;
  void ClearCloseHandler() noexcept;

  std::shared_ptr<State> m_state{};
  Core::ServiceScopeId m_id{};
  Core::ServiceScopeKind m_kind{Core::ServiceScopeKind::Activation};

  friend class HostedUIServiceProvider;
  friend class HostedWindow;
  template <typename T> friend class HostedViewModelHost;
};

/// @brief Hosted window handle paired with its Core service scope.
class HostedWindow final {
public:
  HostedWindow() noexcept = default;

  [[nodiscard]] auto UI() const noexcept -> Window *;
  [[nodiscard]] auto ScopeId() const noexcept -> Core::ServiceScopeId;
  [[nodiscard]] auto IsOpen() const noexcept -> bool;
  [[nodiscard]] auto CreatePageScope(std::string owner) const noexcept
      -> Core::CoreResult<HostedUIScope>;
  auto Close() noexcept -> Core::CoreResult<void>;

  template <typename T>
  [[nodiscard]] auto ResolveRequired(std::string_view name = {}) const noexcept
      -> Core::CoreResult<NGIN::Memory::Shared<std::remove_cvref_t<T>>> {
    auto *provider = Provider();
    if (provider == nullptr || !IsOpen()) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(
              Core::KernelErrorCode::InvalidState, "NGIN.UI.Hosting",
              "HostedWindow::ResolveRequired", "hosted UI window is not open"));
    }
    return provider->ResolveRequired<T>(name, ScopeId());
  }

private:
  HostedWindow(std::shared_ptr<HostedUIScope::State> state,
               Window *window) noexcept;
  [[nodiscard]] auto Provider() const noexcept -> Core::IServiceProvider *;

  std::shared_ptr<HostedUIScope::State> m_state{};
  Window *m_window{nullptr};

  friend class HostedUIServiceProvider;
};

/// @brief Core provider adapter that owns application, window, page, and
/// activation scopes for hosted UI work.
class HostedUIServiceProvider final {
public:
  explicit HostedUIServiceProvider(
      NGIN::Memory::Shared<HostedUIRuntime> runtime);
  ~HostedUIServiceProvider();

  [[nodiscard]] auto CreateWindow(const WindowCreateInfo &info) noexcept
      -> Core::CoreResult<HostedWindow>;
  [[nodiscard]] auto CreatePageScope(Window &window, std::string owner) noexcept
      -> Core::CoreResult<HostedUIScope>;
  [[nodiscard]] auto CreateActivationScope(const HostedUIScope &parent,
                                           std::string owner) noexcept
      -> Core::CoreResult<HostedUIScope>;
  auto CloseWindow(Window &window) noexcept -> Core::CoreResult<void>;
  auto ReconcileClosedWindows() noexcept -> Core::CoreResult<void>;
  void BeginShutdown() noexcept;
  auto DrainAndShutdown() noexcept -> Core::CoreResult<void>;

  [[nodiscard]] auto ApplicationScopeId() const noexcept
      -> Core::ServiceScopeId;
  [[nodiscard]] auto ActiveScopeCount() const noexcept -> UIntSize;
  [[nodiscard]] auto AcceptsWork() const noexcept -> bool;

  template <typename T>
  [[nodiscard]] auto ResolveRequired(std::string_view name = {}) const noexcept
      -> Core::CoreResult<NGIN::Memory::Shared<std::remove_cvref_t<T>>> {
    auto *provider = Provider();
    const auto scope = ApplicationScopeId();
    if (provider == nullptr || scope.IsGlobal()) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::InvalidState,
                                "NGIN.UI.Hosting",
                                "HostedUIServiceProvider::ResolveRequired",
                                "hosted UI service provider is not active"));
    }
    return provider->ResolveRequired<T>(name, scope);
  }

private:
  auto Bind(Core::IServiceRegistry &registry) noexcept
      -> Core::CoreResult<void>;
  [[nodiscard]] auto Provider() const noexcept -> Core::IServiceProvider *;

  std::shared_ptr<HostedUIScope::State> m_state{};

  friend class UIModule;
};

/// @brief Converts a Core-owned object to an aliasing standard shared owner.
template <typename T>
[[nodiscard]] auto ToStdShared(NGIN::Memory::Shared<T> value)
    -> std::shared_ptr<T> {
  if (!value) {
    return {};
  }
  auto owner = std::make_shared<NGIN::Memory::Shared<T>>(std::move(value));
  return std::shared_ptr<T>{owner, owner->Get()};
}

/// @brief Converts a standard shared owner to an aliasing Core shared owner.
template <typename T>
[[nodiscard]] auto ToCoreShared(std::shared_ptr<T> value)
    -> NGIN::Memory::Shared<T> {
  if (!value) {
    return {};
  }
  auto *object = value.get();
  return NGIN::Memory::MakeSharedAlias<T>(object, std::move(value));
}

/// @brief Resolves and owns one DI-created ViewModel for one mounted UI scope.
template <typename T> class HostedViewModelHost final {
public:
  HostedViewModelHost(NGIN::Async::TaskContext context, HostedUIScope scope,
                      InvalidationScheduler scheduler = {})
      : m_state(std::make_shared<State>(std::move(context), std::move(scope),
                                        std::move(scheduler))) {}

  HostedViewModelHost(const HostedViewModelHost &) = delete;
  HostedViewModelHost(HostedViewModelHost &&) = delete;
  auto operator=(const HostedViewModelHost &) -> HostedViewModelHost & = delete;
  auto operator=(HostedViewModelHost &&) -> HostedViewModelHost & = delete;
  ~HostedViewModelHost() { Unmount(); }

  [[nodiscard]] auto Mount(std::string_view serviceName = {}) noexcept
      -> Core::CoreResult<NGIN::Memory::Shared<T>> {
    if (!m_state || m_state->closing || !m_state->scope.IsActive()) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::InvalidState,
                                "NGIN.UI.Hosting", "HostedViewModelHost::Mount",
                                "ViewModel host is closing or inactive"));
    }
    if (m_state->viewModel) {
      return m_state->viewModel;
    }
    auto resolved = m_state->scope.template ResolveRequired<T>(serviceName);
    if (!resolved) {
      return NGIN::Utilities::Unexpected<Core::KernelError>(resolved.Error());
    }
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      m_state->viewModel = resolved.Value();
      m_state->activeTasks = std::make_unique<ViewModelTaskScope>(
          m_state->context, m_state->scheduler);
      const auto weak = std::weak_ptr<State>{m_state};
      auto closeHandler = m_state->scope.SetCloseHandler([weak] {
        if (const auto state = weak.lock()) {
          BeginClose(state);
        }
      });
      if (!closeHandler) {
        m_state->activeTasks.reset();
        m_state->viewModel.Reset();
        return NGIN::Utilities::Unexpected<Core::KernelError>(
            closeHandler.Error());
      }
      if constexpr (requires(T &value, ViewModelTaskScope &scope) {
                      { value.Activate(scope) } noexcept -> std::same_as<void>;
                    }) {
        m_state->viewModel->Activate(*m_state->activeTasks);
      }
      if constexpr (requires(T &value, NGIN::Async::TaskContext &context) {
                      {
                        value.ActivateAsync(context)
                      } -> std::same_as<ViewModelTaskScope::Task>;
                    }) {
        const auto retained = m_state->viewModel;
        static_cast<void>(m_state->activeTasks->Start(
            [retained](NGIN::Async::TaskContext &context) {
              return retained->ActivateAsync(context);
            }));
      }
      return m_state->viewModel;
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      m_state->scope.ClearCloseHandler();
      m_state->activeTasks.reset();
      m_state->viewModel.Reset();
      return NGIN::Utilities::Unexpected<Core::KernelError>(
          Core::MakeKernelError(Core::KernelErrorCode::InternalError,
                                "NGIN.UI.Hosting", "HostedViewModelHost::Mount",
                                "ViewModel activation allocation failed"));
    }
#endif
  }

  void Unmount() noexcept {
    if (m_state) {
      BeginClose(m_state);
    }
  }

  [[nodiscard]] auto Current() const noexcept
      -> const NGIN::Memory::Shared<T> & {
    return m_state->viewModel;
  }
  [[nodiscard]] auto IsMounted() const noexcept -> bool {
    return m_state && static_cast<bool>(m_state->viewModel) &&
           !m_state->closing;
  }
  [[nodiscard]] auto IsClosing() const noexcept -> bool {
    return m_state && m_state->closing && !m_state->closed;
  }
  [[nodiscard]] auto ActiveTaskStatus() const -> const ViewModelTaskStatus & {
    static const ViewModelTaskStatus inactive{.acceptsWork = false};
    return m_state && m_state->activeTasks ? m_state->activeTasks->Status()
                                           : inactive;
  }
  [[nodiscard]] auto CleanupTaskStatus() const -> const ViewModelTaskStatus & {
    static const ViewModelTaskStatus inactive{.acceptsWork = false};
    return m_state && m_state->cleanupTasks ? m_state->cleanupTasks->Status()
                                            : inactive;
  }

private:
  struct State final {
    State(NGIN::Async::TaskContext taskContext, HostedUIScope serviceScope,
          InvalidationScheduler invalidationScheduler)
        : context(std::move(taskContext)), scope(std::move(serviceScope)),
          scheduler(std::move(invalidationScheduler)) {}

    NGIN::Async::TaskContext context;
    HostedUIScope scope{};
    InvalidationScheduler scheduler{};
    std::unique_ptr<ViewModelTaskScope> activeTasks{};
    std::unique_ptr<ViewModelTaskScope> cleanupTasks{};
    NGIN::Memory::Shared<T> viewModel{};
    bool closing{false};
    bool closed{false};
  };

  static void Finalize(const std::shared_ptr<State> &state) noexcept {
    state->viewModel.Reset();
    state->scope.ClearCloseHandler();
    static_cast<void>(state->scope.End());
    state->closed = true;
  }

  static void
  StartAsyncDeactivation(const std::shared_ptr<State> &state) noexcept {
    if constexpr (requires(T &value, NGIN::Async::TaskContext &context) {
                    {
                      value.DeactivateAsync(context)
                    } -> std::same_as<ViewModelTaskScope::Task>;
                  }) {
#if NGIN_ASYNC_HAS_EXCEPTIONS
      try {
#endif
        state->cleanupTasks = std::make_unique<ViewModelTaskScope>(
            state->context, state->scheduler);
        const auto retained = state->viewModel;
        const auto weak = std::weak_ptr<State>{state};
        const auto task = state->cleanupTasks->Start(
            [retained](NGIN::Async::TaskContext &context) {
              return retained->DeactivateAsync(context);
            },
            [weak](const ViewModelTaskOutcome &) {
              if (const auto current = weak.lock()) {
                current->cleanupTasks->CancelAll();
                Finalize(current);
              }
            });
        if (task) {
          return;
        }
#if NGIN_ASYNC_HAS_EXCEPTIONS
      } catch (...) {
      }
#endif
    }
    Finalize(state);
  }

  static void BeginClose(const std::shared_ptr<State> &state) noexcept {
    if (!state || state->closing || state->closed) {
      return;
    }
    state->closing = true;
    if (state->viewModel) {
      if constexpr (requires(T &value) {
                      { value.Deactivate() } noexcept -> std::same_as<void>;
                    }) {
        state->viewModel->Deactivate();
      }
    }
    if (!state->activeTasks) {
      StartAsyncDeactivation(state);
      return;
    }
    state->activeTasks->Close([state] { StartAsyncDeactivation(state); });
  }

  std::shared_ptr<State> m_state{};
};

/// @brief UI application, text, and pump configuration for Core hosting.
struct UIHostingCreateInfo final {
  ApplicationCreateInfo application{};
  NativeTextCreateInfo text{};
  std::chrono::milliseconds maximumWait{250};
};

/// @brief Owns the NGIN.UI application and native text services exposed to
/// Core.
class HostedUIRuntime final {
public:
  [[nodiscard]] static auto Create(UIHostingCreateInfo info) noexcept
      -> UIResult<NGIN::Memory::Shared<HostedUIRuntime>>;

  HostedUIRuntime(std::unique_ptr<Application> application,
                  std::unique_ptr<NativeTextSystem> text) noexcept;

  [[nodiscard]] auto UI() noexcept -> Application &;
  [[nodiscard]] auto Text() noexcept -> NativeTextSystem &;

private:
  std::unique_ptr<Application> m_application{};
  std::unique_ptr<NativeTextSystem> m_text{};
};

/// @brief Non-owning Core service wrapper for the active platform backend.
class PlatformBackendReference final {
public:
  explicit PlatformBackendReference(IPlatformBackend &backend) noexcept;
  [[nodiscard]] auto Get() const noexcept -> IPlatformBackend &;

private:
  IPlatformBackend *m_backend{nullptr};
};

/// @brief Non-owning Core service wrapper for the active render backend.
class RenderBackendReference final {
public:
  explicit RenderBackendReference(IRenderBackend &backend) noexcept;
  [[nodiscard]] auto Get() const noexcept -> IRenderBackend &;

private:
  IRenderBackend *m_backend{nullptr};
};

/// @brief Thread-safe posting boundary that schedules work on the UI thread.
class IUIDispatcher {
public:
  virtual ~IUIDispatcher() = default;

  virtual auto Post(NGIN::Utilities::Callable<void()> callback) noexcept
      -> Core::CoreResult<void> = 0;
  virtual auto Drain() noexcept -> Core::CoreResult<void> = 0;
  virtual void Wake() noexcept = 0;
  virtual void BindCurrentThread() noexcept = 0;
  [[nodiscard]] virtual auto IsCurrentThread() const noexcept -> bool = 0;
};

/// @brief Queue-backed dispatcher integrated with the hosted UI event loop.
class UIDispatcher final : public IUIDispatcher {
public:
  explicit UIDispatcher(NGIN::Memory::Shared<HostedUIRuntime> runtime);
  ~UIDispatcher() override;

  auto Post(NGIN::Utilities::Callable<void()> callback) noexcept
      -> Core::CoreResult<void> override;
  auto Drain() noexcept -> Core::CoreResult<void> override;
  void Wake() noexcept override;
  void BindCurrentThread() noexcept override;
  [[nodiscard]] auto IsCurrentThread() const noexcept -> bool override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

/// @brief Core host run loop that pumps UI events and dispatched callbacks.
class UIHostRunLoop final : public Core::IHostRunLoop {
public:
  UIHostRunLoop(NGIN::Memory::Shared<HostedUIRuntime> runtime,
                NGIN::Memory::Shared<IUIDispatcher> dispatcher,
                NGIN::Memory::Shared<HostedUIServiceProvider> services,
                std::chrono::milliseconds maximumWait) noexcept;

  auto Run(Core::IApplicationHost &host) noexcept
      -> Core::CoreResult<void> override;
  void Wake() noexcept override;

private:
  NGIN::Memory::Shared<HostedUIRuntime> m_runtime{};
  NGIN::Memory::Shared<IUIDispatcher> m_dispatcher{};
  NGIN::Memory::Shared<HostedUIServiceProvider> m_services{};
  std::chrono::milliseconds m_maximumWait{250};
};

/// @brief Core module that publishes and manages hosted UI services.
class UIModule final : public Core::IModule {
public:
  UIModule(NGIN::Memory::Shared<HostedUIRuntime> runtime,
           NGIN::Memory::Shared<IUIDispatcher> dispatcher,
           NGIN::Memory::Shared<HostedUIServiceProvider> services) noexcept;

  auto OnRegister(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;
  auto OnStart(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;
  auto OnStop(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;
  auto OnShutdown(Core::ModuleContext &context) noexcept
      -> Core::CoreResult<void> override;

private:
  NGIN::Memory::Shared<HostedUIRuntime> m_runtime{};
  NGIN::Memory::Shared<IUIDispatcher> m_dispatcher{};
  NGIN::Memory::Shared<HostedUIServiceProvider> m_services{};
};

/// @brief Ownership bundle returned after UI services are registered with Core.
struct UIHostingRegistration final {
  NGIN::Memory::Shared<HostedUIRuntime> runtime{};
  NGIN::Memory::Shared<IUIDispatcher> dispatcher{};
  NGIN::Memory::Shared<HostedUIServiceProvider> services{};
  std::shared_ptr<UIHostRunLoop> runLoop{};
};

/// @brief Page activation context bound to one hosted window and its task
/// scheduler.
class HostedNavigationContext final : public PageActivationContext {
public:
  HostedNavigationContext(NGIN::Memory::Shared<HostedUIRuntime> runtime,
                          HostedWindow window) noexcept
      : m_runtime(std::move(runtime)), m_window(std::move(window)) {}

  [[nodiscard]] auto Runtime() const noexcept
      -> const NGIN::Memory::Shared<HostedUIRuntime> & {
    return m_runtime;
  }
  [[nodiscard]] auto WindowHandle() noexcept -> HostedWindow & {
    return m_window;
  }

private:
  NGIN::Memory::Shared<HostedUIRuntime> m_runtime{};
  HostedWindow m_window{};
};

/// @brief Maps a structured Core failure into a structured UI failure.
[[nodiscard]] auto MakeHostedUIError(const Core::KernelError &error,
                                     const char *operation) noexcept -> UIError;

/// @brief Application-builder extension for hosted services and typed pages.
class HostedPageBuilder final {
public:
  HostedPageBuilder(Core::ApplicationBuilder &builder,
                    PageRegistry &pages) noexcept
      : m_builder(&builder), m_pages(&pages) {}

  /// @brief Returns the Core service collection used by page dependencies.
  [[nodiscard]] auto Services() noexcept -> Core::ServiceCollection & {
    return m_builder->Services();
  }

  /// @brief Registers a transient DI-created ViewModel and its typed page.
  template <typename TPage, typename TViewModel,
            typename TParameter = NoNavigationParameter, typename Compose>
  [[nodiscard]] auto AddPage(PageRegistrationOptions options, Compose compose,
                             std::string serviceName = {}) -> UIResult<void> {
    auto registered = UsePage<TPage, TViewModel, TParameter>(
        std::move(options), std::move(compose), serviceName);
    if (!registered) {
      return registered;
    }
    m_builder->Services().template AddTransient<TViewModel>(
        std::move(serviceName));
    return {};
  }

  /// @brief Registers a page for an already configured ViewModel service.
  template <typename TPage, typename TViewModel,
            typename TParameter = NoNavigationParameter, typename Compose>
  [[nodiscard]] auto UsePage(PageRegistrationOptions options, Compose compose,
                             std::string serviceName = {}) -> UIResult<void> {
    using ViewModel = std::remove_cvref_t<TViewModel>;
    return m_pages->template Register<TPage, ViewModel, TParameter>(
        std::move(options),
        [serviceName = std::move(serviceName)](
            PageActivationContext &activation, const TParameter &,
            const std::string_view entryKey) -> UIResult<PageLease<ViewModel>> {
          auto *hosted = dynamic_cast<HostedNavigationContext *>(&activation);
          if (hosted == nullptr || !hosted->Runtime() ||
              !hosted->WindowHandle().IsOpen()) {
            return MakeUIError(UIErrorCode::InvalidState,
                               "Hosted page needs an open hosted window",
                               "NGIN.UI.Hosting", "HostedPageBuilder::UsePage");
          }
          auto scope =
              hosted->WindowHandle().CreatePageScope(std::string{entryKey});
          if (!scope) {
            return MakeHostedUIError(scope.Error(),
                                     "HostedPageBuilder::CreatePageScope");
          }
#if NGIN_ASYNC_HAS_EXCEPTIONS
          try {
#endif
            auto host = std::make_shared<HostedViewModelHost<ViewModel>>(
                hosted->Runtime()->UI().CreateTaskContext(
                    *hosted->WindowHandle().UI()),
                std::move(scope).Value());
            auto mounted = host->Mount(serviceName);
            if (!mounted) {
              return MakeHostedUIError(mounted.Error(),
                                       "HostedPageBuilder::Mount");
            }
            return PageLease<ViewModel>{
                .viewModel = ToStdShared(std::move(mounted).Value()),
                .close = [host] { host->Unmount(); },
            };
#if NGIN_ASYNC_HAS_EXCEPTIONS
          } catch (...) {
            return MakeUIError(UIErrorCode::OutOfMemory,
                               "Hosted page activation allocation failed",
                               "NGIN.UI.Hosting", "HostedPageBuilder::UsePage");
          }
#endif
        },
        std::move(compose));
  }

private:
  Core::ApplicationBuilder *m_builder{nullptr};
  PageRegistry *m_pages{nullptr};
};

/// @brief Creates the typed page extension for a Core application builder.
[[nodiscard]] inline auto ConfigureUIPages(Core::ApplicationBuilder &builder,
                                           PageRegistry &pages) noexcept
    -> HostedPageBuilder {
  return HostedPageBuilder{builder, pages};
}

/// @brief Binds one navigation activation context to a hosted window.
[[nodiscard]] inline auto
CreateHostedNavigationContext(const UIHostingRegistration &hosting,
                              HostedWindow window) noexcept
    -> UIResult<HostedNavigationContext> {
  if (!hosting.runtime || !window.IsOpen()) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Hosted navigation needs a runtime and open window",
                       "NGIN.UI.Hosting", "CreateHostedNavigationContext");
  }
  return HostedNavigationContext{hosting.runtime, std::move(window)};
}

[[nodiscard]] auto ConfigureUIHosting(Core::ApplicationBuilder &builder,
                                      UIHostingCreateInfo info) noexcept
    -> UIResult<UIHostingRegistration>;
} // namespace NGIN::UI::Hosting
