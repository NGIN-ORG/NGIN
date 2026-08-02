#include <NGIN/UI/Command.hpp>

#include <algorithm>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto ExpiredStatus() noexcept -> CommandStatus {
  return CommandStatus{
      .enabled = false,
      .canExecute = false,
  };
}

[[nodiscard]] auto FaultError(const NGIN::Async::AsyncFault &fault)
    -> CommandError {
  const auto message = fault.message.empty()
                           ? std::string_view{"Asynchronous command failed"}
                           : std::string_view{fault.message};
  return CommandError{
      .kind = CommandErrorKind::Fault,
      .code = NGIN::Text::String{"async-fault"},
      .message = NGIN::Text::String{message},
      .nativeCode = static_cast<Int32>(fault.native),
  };
}

[[nodiscard]] auto ExceptionError() -> CommandError {
  return CommandError{
      .kind = CommandErrorKind::Fault,
      .code = NGIN::Text::String{"unhandled-exception"},
      .message = NGIN::Text::String{"Command action threw an exception"},
  };
}
} // namespace

CommandBinding::CommandBinding(ExecuteCallback execute, CancelCallback cancel,
                               StatusGetter status, Subscriber subscriber)
    : m_execute(std::move(execute)), m_cancel(std::move(cancel)),
      m_status(std::move(status)), m_subscriber(std::move(subscriber)) {}

auto CommandBinding::Execute() const -> CommandInvocation {
  return m_execute ? m_execute() : CommandInvocation::RejectedExpired;
}

void CommandBinding::Cancel() const noexcept {
  if (m_cancel) {
    m_cancel();
  }
}

auto CommandBinding::Status() const -> CommandStatus {
  return m_status ? m_status() : ExpiredStatus();
}

auto CommandBinding::CanExecute() const -> bool { return Status().canExecute; }

auto CommandBinding::IsRunning() const -> bool { return Status().isRunning; }

auto CommandBinding::Subscribe(StateObserver<CommandStatus> observer) const
    -> Subscription {
  return m_subscriber ? m_subscriber(std::move(observer)) : Subscription{};
}

CommandBinding::operator bool() const noexcept {
  return static_cast<bool>(m_execute) && static_cast<bool>(m_status);
}

struct Command::Storage final {
  Storage(Action commandAction, const bool initiallyEnabled,
          InvalidationScheduler scheduler)
      : action(std::move(commandAction)),
        status(
            CommandStatus{
                .enabled = initiallyEnabled,
                .canExecute = initiallyEnabled,
            },
            std::move(scheduler)) {}

  Action action{};
  State<CommandStatus> status;
  bool alive{true};
};

Command::Command(Action action, const bool enabled,
                 InvalidationScheduler scheduler)
    : m_storage(std::make_shared<Storage>(std::move(action), enabled,
                                          std::move(scheduler))) {}

Command::Command(NGIN::Utilities::Callable<void()> action, const bool enabled,
                 InvalidationScheduler scheduler)
    : Command(
          Action{[action = std::move(action)]() mutable -> CommandResult<void> {
            action();
            return {};
          }},
          enabled, std::move(scheduler)) {}

Command::~Command() {
  if (m_storage) {
    m_storage->alive = false;
    m_storage->action = nullptr;
  }
}

auto Command::ExecuteStorage(const std::shared_ptr<Storage> &storage)
    -> CommandInvocation {
  if (!storage || !storage->alive || !storage->status.Get().canExecute) {
    return CommandInvocation::RejectedDisabled;
  }

  auto running = storage->status.Get();
  running.isRunning = true;
  running.canExecute = false;
  running.executionId += 1;
  running.lastOutcome = {};
  static_cast<void>(storage->status.Set(std::move(running)));

  auto outcome = CommandOutcome{.kind = CommandOutcomeKind::Succeeded};
#if NGIN_ASYNC_HAS_EXCEPTIONS
  try {
#endif
    auto result = storage->action();
    if (!result) {
      auto error = std::move(result).Error();
      outcome.kind = error.kind == CommandErrorKind::Domain
                         ? CommandOutcomeKind::DomainError
                         : CommandOutcomeKind::Fault;
      outcome.error = std::move(error);
    }
#if NGIN_ASYNC_HAS_EXCEPTIONS
  } catch (...) {
    outcome.kind = CommandOutcomeKind::Fault;
    outcome.error = ExceptionError();
  }
#endif

  if (storage->alive) {
    auto finished = storage->status.Get();
    finished.isRunning = false;
    finished.canExecute = finished.enabled;
    finished.lastOutcome = std::move(outcome);
    static_cast<void>(storage->status.Set(std::move(finished)));
  }
  return CommandInvocation::Started;
}

auto Command::Execute() -> CommandInvocation {
  return ExecuteStorage(m_storage);
}

void Command::SetEnabled(const bool enabled) {
  if (!m_storage || !m_storage->alive) {
    return;
  }
  auto status = m_storage->status.Get();
  status.enabled = enabled;
  status.canExecute = enabled && !status.isRunning;
  static_cast<void>(m_storage->status.Set(std::move(status)));
}

auto Command::Status() const -> const CommandStatus & {
  static const auto expired = ExpiredStatus();
  return m_storage ? m_storage->status.Get() : expired;
}

auto Command::Subscribe(StateObserver<CommandStatus> observer) -> Subscription {
  return m_storage ? m_storage->status.Subscribe(std::move(observer))
                   : Subscription{};
}

auto Command::AsBinding() const -> CommandBinding {
  const auto weak = std::weak_ptr<Storage>{m_storage};
  return CommandBinding{
      [weak] {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          return ExecuteStorage(storage);
        }
        return CommandInvocation::RejectedExpired;
      },
      {},
      [weak] {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          return storage->status.Get();
        }
        return ExpiredStatus();
      },
      [weak](StateObserver<CommandStatus> observer) {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          return storage->status.Subscribe(std::move(observer));
        }
        return Subscription{};
      },
  };
}

struct AsyncCommand::Run final {
  Run(const UInt64 runId, NGIN::Async::TaskContext runContext)
      : id(runId), context(std::move(runContext)) {}

  UInt64 id{0};
  NGIN::Async::CancellationSource cancellation{};
  NGIN::Async::TaskContext context;
};

struct AsyncCommand::Storage final {
  Storage(NGIN::Async::TaskContext taskContext, Action commandAction,
          const bool initiallyEnabled,
          const CommandConcurrencyPolicy concurrencyPolicy,
          const UIntSize maximumQueue, InvalidationScheduler scheduler)
      : context(std::move(taskContext)), action(std::move(commandAction)),
        concurrency(concurrencyPolicy), queueCapacity(maximumQueue),
        status(
            CommandStatus{
                .enabled = initiallyEnabled,
                .canExecute = initiallyEnabled,
            },
            std::move(scheduler)) {}

  [[nodiscard]] auto ComputeCanExecute() const noexcept -> bool {
    if (!alive || !status.Get().enabled) {
      return false;
    }
    if (!active) {
      return true;
    }
    switch (concurrency) {
    case CommandConcurrencyPolicy::Reject:
      return false;
    case CommandConcurrencyPolicy::CancelPrevious:
      return true;
    case CommandConcurrencyPolicy::Queue:
      return pendingCount < queueCapacity;
    }
    return false;
  }

  void Publish() {
    auto next = status.Get();
    next.canExecute = ComputeCanExecute();
    next.isRunning = static_cast<bool>(active);
    next.canCancel = static_cast<bool>(active);
    next.queuedCount = pendingCount;
    static_cast<void>(status.Set(std::move(next)));
  }

  NGIN::Async::TaskContext context;
  Action action{};
  CommandConcurrencyPolicy concurrency{CommandConcurrencyPolicy::Reject};
  UIntSize queueCapacity{1};
  State<CommandStatus> status;
  std::shared_ptr<AsyncCommand::Run> active{};
  UIntSize pendingCount{0};
  UInt64 nextExecutionId{1};
  bool alive{true};
};

void AsyncCommand::FinishRun(const std::shared_ptr<Storage> &storage,
                             const std::shared_ptr<Run> &run,
                             CommandOutcome outcome) {
  if (!storage->active || storage->active->id != run->id) {
    return;
  }

  storage->active.reset();
  auto status = storage->status.Get();
  status.lastOutcome = std::move(outcome);
  static_cast<void>(storage->status.Set(std::move(status)));

  if (storage->alive && storage->status.Get().enabled &&
      storage->pendingCount > 0) {
    storage->pendingCount -= 1;
    StartRun(storage);
    return;
  }

  if (!storage->status.Get().enabled) {
    storage->pendingCount = 0;
  }
  storage->Publish();
}

auto AsyncCommand::RunAction(std::shared_ptr<Storage> storage,
                             std::shared_ptr<Run> run)
    -> NGIN::Async::Task<void> {
  auto outcome = CommandOutcome{.kind = CommandOutcomeKind::Succeeded};
#if NGIN_ASYNC_HAS_EXCEPTIONS
  try {
#endif
    auto operation =
        NGIN::Async::Spawn(run->context, storage->action(run->context));
    auto completion = co_await operation;
    if (completion.IsDomainError()) {
      outcome.kind = CommandOutcomeKind::DomainError;
      outcome.error = std::move(completion).DomainError();
    } else if (completion.IsCanceled()) {
      outcome.kind = CommandOutcomeKind::Canceled;
    } else if (completion.IsFault()) {
      outcome.kind = CommandOutcomeKind::Fault;
      outcome.error = FaultError(completion.Fault());
    }
#if NGIN_ASYNC_HAS_EXCEPTIONS
  } catch (...) {
    outcome.kind = CommandOutcomeKind::Fault;
    outcome.error = ExceptionError();
  }
#endif

  FinishRun(storage, run, std::move(outcome));
}

void AsyncCommand::StartRun(const std::shared_ptr<Storage> &storage) {
  const auto id = storage->nextExecutionId++;
  auto run = std::make_shared<Run>(id, storage->context);
  run->context.BindLinkedCancellationToken(run->cancellation.GetToken());
  storage->active = run;

  auto status = storage->status.Get();
  status.executionId = id;
  status.lastOutcome = {};
  static_cast<void>(storage->status.Set(std::move(status)));
  storage->Publish();

  NGIN::Async::Detach(run->context, RunAction(storage, run));
}

AsyncCommand::AsyncCommand(NGIN::Async::TaskContext context, Action action,
                           const bool enabled,
                           const CommandConcurrencyPolicy concurrency,
                           const UIntSize queueCapacity,
                           InvalidationScheduler scheduler)
    : m_storage(std::make_shared<Storage>(
          std::move(context), std::move(action), enabled, concurrency,
          std::max<UIntSize>(queueCapacity, 1), std::move(scheduler))) {}

AsyncCommand::~AsyncCommand() {
  if (!m_storage) {
    return;
  }
  m_storage->alive = false;
  m_storage->pendingCount = 0;
  if (m_storage->active) {
    m_storage->active->cancellation.Cancel();
  }
  m_storage->action = nullptr;
  m_storage->Publish();
}

auto AsyncCommand::Execute() -> CommandInvocation {
  return ExecuteStorage(m_storage);
}

auto AsyncCommand::ExecuteStorage(const std::shared_ptr<Storage> &storage)
    -> CommandInvocation {
  if (!storage || !storage->alive || !storage->status.Get().enabled) {
    return CommandInvocation::RejectedDisabled;
  }

  if (!storage->active) {
    StartRun(storage);
    return CommandInvocation::Started;
  }

  switch (storage->concurrency) {
  case CommandConcurrencyPolicy::Reject:
    return CommandInvocation::RejectedRunning;
  case CommandConcurrencyPolicy::CancelPrevious:
    storage->active->cancellation.Cancel();
    StartRun(storage);
    return CommandInvocation::Replaced;
  case CommandConcurrencyPolicy::Queue:
    if (storage->pendingCount >= storage->queueCapacity) {
      return CommandInvocation::RejectedQueueFull;
    }
    storage->pendingCount += 1;
    storage->Publish();
    return CommandInvocation::Queued;
  }
  return CommandInvocation::RejectedRunning;
}

void AsyncCommand::Cancel() noexcept { CancelStorage(m_storage); }

void AsyncCommand::CancelStorage(
    const std::shared_ptr<Storage> &storage) noexcept {
  if (!storage || !storage->alive) {
    return;
  }
  storage->pendingCount = 0;
  if (storage->active) {
    storage->active->cancellation.Cancel();
  }
  storage->Publish();
}

void AsyncCommand::SetEnabled(const bool enabled) {
  if (!m_storage || !m_storage->alive) {
    return;
  }
  auto status = m_storage->status.Get();
  status.enabled = enabled;
  static_cast<void>(m_storage->status.Set(std::move(status)));
  if (!enabled) {
    m_storage->pendingCount = 0;
  }
  m_storage->Publish();
}

auto AsyncCommand::Status() const -> const CommandStatus & {
  static const auto expired = ExpiredStatus();
  return m_storage ? m_storage->status.Get() : expired;
}

auto AsyncCommand::Subscribe(StateObserver<CommandStatus> observer)
    -> Subscription {
  return m_storage ? m_storage->status.Subscribe(std::move(observer))
                   : Subscription{};
}

auto AsyncCommand::AsBinding() const -> CommandBinding {
  const auto weak = std::weak_ptr<Storage>{m_storage};
  return CommandBinding{
      [weak] {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          return ExecuteStorage(storage);
        }
        return CommandInvocation::RejectedExpired;
      },
      [weak] {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          CancelStorage(storage);
        }
      },
      [weak] {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          return storage->status.Get();
        }
        return ExpiredStatus();
      },
      [weak](StateObserver<CommandStatus> observer) {
        if (const auto storage = weak.lock(); storage && storage->alive) {
          return storage->status.Subscribe(std::move(observer));
        }
        return Subscription{};
      },
  };
}
} // namespace NGIN::UI
