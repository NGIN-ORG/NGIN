#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <concepts>
#include <memory>
#include <typeindex>
#include <utility>

namespace NGIN::UI {
/// @brief Final result of one task owned by a ViewModel task scope.
enum class ViewModelTaskOutcomeKind : UInt8 {
  None,
  Succeeded,
  DomainError,
  Canceled,
  Fault,
};

/// @brief Observable completion retained by a ViewModel task scope.
struct ViewModelTaskOutcome final {
  UInt64 taskId{0};
  ViewModelTaskOutcomeKind kind{ViewModelTaskOutcomeKind::None};
  std::optional<CommandError> error{};

  [[nodiscard]] auto operator==(const ViewModelTaskOutcome &) const noexcept
      -> bool = default;
};

/// @brief Aggregate diagnostics for work owned by one mounted ViewModel.
struct ViewModelTaskStatus final {
  UIntSize activeCount{0};
  UInt64 startedCount{0};
  UInt64 succeededCount{0};
  UInt64 canceledCount{0};
  UInt64 failedCount{0};
  bool acceptsWork{true};
  ViewModelTaskOutcome lastOutcome{};

  [[nodiscard]] auto operator==(const ViewModelTaskStatus &) const noexcept
      -> bool = default;
};

/// @brief Lifetime-safe handle for canceling one task without closing its
/// scope.
class ViewModelTaskHandle final {
public:
  using CancelCallback = NGIN::Utilities::Callable<void()>;
  using RunningGetter = NGIN::Utilities::Callable<bool()>;

  ViewModelTaskHandle() noexcept = default;
  ViewModelTaskHandle(UInt64 taskId, CancelCallback cancel,
                      RunningGetter running)
      : m_taskId(taskId), m_cancel(std::move(cancel)),
        m_running(std::move(running)) {}

  void Cancel() const noexcept {
    if (m_cancel) {
      m_cancel();
    }
  }
  [[nodiscard]] auto IsRunning() const noexcept -> bool {
    return m_running && m_running();
  }
  [[nodiscard]] auto TaskId() const noexcept -> UInt64 { return m_taskId; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return m_taskId != 0 && static_cast<bool>(m_cancel);
  }

private:
  UInt64 m_taskId{0};
  CancelCallback m_cancel{};
  RunningGetter m_running{};
};

/// @brief Owns, schedules, observes, and cancels UI-bound ViewModel tasks.
class ViewModelTaskScope final {
public:
  using Task = NGIN::Async::Task<void, CommandError>;
  using Work = NGIN::Utilities::Callable<Task(NGIN::Async::TaskContext &)>;
  using Observer =
      NGIN::Utilities::Callable<void(const ViewModelTaskOutcome &)>;
  using DrainedObserver = NGIN::Utilities::Callable<void()>;

  explicit ViewModelTaskScope(NGIN::Async::TaskContext context,
                              InvalidationScheduler scheduler = {});
  ViewModelTaskScope(const ViewModelTaskScope &) = delete;
  ViewModelTaskScope(ViewModelTaskScope &&) = delete;
  auto operator=(const ViewModelTaskScope &) -> ViewModelTaskScope & = delete;
  auto operator=(ViewModelTaskScope &&) -> ViewModelTaskScope & = delete;
  ~ViewModelTaskScope();

  /// @brief Starts work in a child context and retains it until observed.
  [[nodiscard]] auto Start(Work work, Observer observer = {})
      -> ViewModelTaskHandle;
  /// @brief Cancels all work and permanently closes this scope to new work.
  void CancelAll() noexcept;
  /// @brief Closes the scope and invokes the observer after all work is
  /// canceled and observed.
  void Close(DrainedObserver observer = {}) noexcept;
  [[nodiscard]] auto IsDrained() const noexcept -> bool;
  [[nodiscard]] auto Status() const -> const ViewModelTaskStatus &;
  [[nodiscard]] auto StatusBinding() const
      -> ReadOnlyBinding<ViewModelTaskStatus>;

private:
  struct Storage;
  struct Run;
  [[nodiscard]] static auto
  StartStorage(const std::shared_ptr<Storage> &storage, Work work,
               Observer observer) -> ViewModelTaskHandle;
  static void CancelStorage(const std::shared_ptr<Storage> &storage) noexcept;
  [[nodiscard]] static auto ObserveRun(std::shared_ptr<Storage> storage,
                                       std::shared_ptr<Run> run)
      -> NGIN::Async::Task<void>;
  static void FinishRun(const std::shared_ptr<Storage> &storage,
                        const std::shared_ptr<Run> &run,
                        ViewModelTaskOutcome outcome);

  std::shared_ptr<Storage> m_storage{};
};

/// @brief Non-owning application hook for optional ViewModel services.
class ViewModelServiceResolver final {
public:
  using Resolver = NGIN::Utilities::Callable<void *(std::type_index)>;

  ViewModelServiceResolver() noexcept = default;
  explicit ViewModelServiceResolver(Resolver resolver)
      : m_resolver(std::move(resolver)) {}

  template <typename T> [[nodiscard]] auto TryResolve() const noexcept -> T * {
    return m_resolver ? static_cast<T *>(m_resolver(std::type_index{typeid(T)}))
                      : nullptr;
  }

private:
  Resolver m_resolver{};
};

/// @brief Factory hook used by a keyed ViewModel host.
template <typename T>
using ViewModelFactory = NGIN::Utilities::Callable<std::shared_ptr<T>(
    const NGIN::Text::String &key, const ViewModelServiceResolver &services)>;

/// @brief Creates, activates, reuses, and releases one keyed plain ViewModel.
template <typename T> class KeyedViewModelHost final {
public:
  KeyedViewModelHost(NGIN::Async::TaskContext context,
                     ViewModelFactory<T> factory,
                     ViewModelServiceResolver services = {},
                     InvalidationScheduler scheduler = {})
      : m_context(std::move(context)), m_factory(std::move(factory)),
        m_services(std::move(services)), m_scheduler(std::move(scheduler)),
        m_cleanupScope(
            std::make_unique<ViewModelTaskScope>(m_context, m_scheduler)) {}

  KeyedViewModelHost(const KeyedViewModelHost &) = delete;
  KeyedViewModelHost(KeyedViewModelHost &&) = delete;
  auto operator=(const KeyedViewModelHost &) -> KeyedViewModelHost & = delete;
  auto operator=(KeyedViewModelHost &&) -> KeyedViewModelHost & = delete;
  ~KeyedViewModelHost() {
    Hide();
    m_cleanupScope->CancelAll();
  }

  /// @brief Reuses the same key or replaces and activates a new ViewModel.
  [[nodiscard]] auto Show(NGIN::Text::String key)
      -> UIResult<std::shared_ptr<T>> {
    if (m_current && m_key == key) {
      return m_current;
    }
    Hide();
    if (!m_factory) {
      return MakeUIError(UIErrorCode::InvalidState,
                         "ViewModel host has no factory", "NGIN.UI",
                         "KeyedViewModelHost::Show");
    }
    auto created = std::shared_ptr<T>{};
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      created = m_factory(key, m_services);
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      return MakeUIError(UIErrorCode::ResourceFailed,
                         "ViewModel factory threw an exception", "NGIN.UI",
                         "KeyedViewModelHost::Show");
    }
#endif
    if (!created) {
      return MakeUIError(UIErrorCode::ResourceFailed,
                         "ViewModel factory returned no value", "NGIN.UI",
                         "KeyedViewModelHost::Show");
    }
    m_key = std::move(key);
    m_current = std::move(created);
    m_activeScope =
        std::make_unique<ViewModelTaskScope>(m_context, m_scheduler);
    if constexpr (requires(T &value, ViewModelTaskScope &scope) {
                    { value.Activate(scope) } noexcept -> std::same_as<void>;
                  }) {
      m_current->Activate(*m_activeScope);
    }
    if constexpr (requires(T &value, NGIN::Async::TaskContext &context) {
                    {
                      value.ActivateAsync(context)
                    } -> std::same_as<ViewModelTaskScope::Task>;
                  }) {
      const auto retained = m_current;
      static_cast<void>(
          m_activeScope->Start([retained](NGIN::Async::TaskContext &context) {
            return retained->ActivateAsync(context);
          }));
    }
    return m_current;
  }

  /// @brief Deactivates, cancels, and releases the mounted ViewModel.
  void Hide() noexcept {
    if (!m_current) {
      return;
    }
    const auto retained = m_current;
    if constexpr (requires(T &value) {
                    { value.Deactivate() } noexcept -> std::same_as<void>;
                  }) {
      retained->Deactivate();
    }
    if (m_activeScope) {
      m_activeScope->CancelAll();
      m_activeScope.reset();
    }
    if constexpr (requires(T &value, NGIN::Async::TaskContext &context) {
                    {
                      value.DeactivateAsync(context)
                    } -> std::same_as<ViewModelTaskScope::Task>;
                  }) {
#if NGIN_ASYNC_HAS_EXCEPTIONS
      try {
#endif
        static_cast<void>(m_cleanupScope->Start(
            [retained](NGIN::Async::TaskContext &context) {
              return retained->DeactivateAsync(context);
            }));
#if NGIN_ASYNC_HAS_EXCEPTIONS
      } catch (...) {
      }
#endif
    }
    m_current.reset();
    m_key = {};
  }

  [[nodiscard]] auto Current() const noexcept -> const std::shared_ptr<T> & {
    return m_current;
  }
  [[nodiscard]] auto CurrentKey() const noexcept -> const NGIN::Text::String & {
    return m_key;
  }
  [[nodiscard]] auto IsMounted() const noexcept -> bool {
    return static_cast<bool>(m_current);
  }
  [[nodiscard]] auto ActiveTaskStatus() const -> const ViewModelTaskStatus & {
    static const ViewModelTaskStatus inactive{.acceptsWork = false};
    return m_activeScope ? m_activeScope->Status() : inactive;
  }
  [[nodiscard]] auto CleanupTaskStatus() const -> const ViewModelTaskStatus & {
    return m_cleanupScope->Status();
  }

private:
  NGIN::Async::TaskContext m_context;
  ViewModelFactory<T> m_factory{};
  ViewModelServiceResolver m_services{};
  InvalidationScheduler m_scheduler{};
  std::unique_ptr<ViewModelTaskScope> m_cleanupScope{};
  std::unique_ptr<ViewModelTaskScope> m_activeScope{};
  std::shared_ptr<T> m_current{};
  NGIN::Text::String m_key{};
};
} // namespace NGIN::UI
