#include <NGIN/UI/ViewModel.hpp>

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto FaultError(const NGIN::Async::AsyncFault &fault)
    -> CommandError {
  const auto message = fault.message.empty()
                           ? std::string_view{"ViewModel task failed"}
                           : std::string_view{fault.message};
  return CommandError{
      .kind = CommandErrorKind::Fault,
      .code = NGIN::Text::String{"viewmodel-task-fault"},
      .message = NGIN::Text::String{message},
      .nativeCode = static_cast<Int32>(fault.native),
  };
}

[[nodiscard]] auto ExceptionError() -> CommandError {
  return CommandError{
      .kind = CommandErrorKind::Fault,
      .code = NGIN::Text::String{"viewmodel-task-exception"},
      .message = NGIN::Text::String{"ViewModel work threw an exception"},
  };
}

[[nodiscard]] auto ObserverExceptionError() -> CommandError {
  return CommandError{
      .kind = CommandErrorKind::Fault,
      .code = NGIN::Text::String{"viewmodel-task-observer-exception"},
      .message =
          NGIN::Text::String{"ViewModel task observer threw an exception"},
  };
}
} // namespace

struct ViewModelTaskScope::Run final {
  Run(const UInt64 runId, NGIN::Async::TaskContext runContext, Work runWork,
      Observer runObserver)
      : id(runId), context(std::move(runContext)), work(std::move(runWork)),
        observer(std::move(runObserver)) {}

  UInt64 id{0};
  NGIN::Async::CancellationSource cancellation{};
  NGIN::Async::TaskContext context;
  Work work{};
  Observer observer{};
  bool running{true};
};

struct ViewModelTaskScope::Storage final {
  Storage(NGIN::Async::TaskContext taskContext, InvalidationScheduler scheduler)
      : context(std::move(taskContext)),
        status(ViewModelTaskStatus{}, std::move(scheduler)) {
    context.BindLinkedCancellationToken(cancellation.GetToken());
  }

  NGIN::Async::TaskContext context;
  NGIN::Async::CancellationSource cancellation{};
  State<ViewModelTaskStatus> status;
  std::vector<std::shared_ptr<Run>> runs{};
  DrainedObserver drainedObserver{};
  UInt64 nextTaskId{1};
  bool alive{true};
  bool acceptsWork{true};
};

ViewModelTaskScope::ViewModelTaskScope(NGIN::Async::TaskContext context,
                                       InvalidationScheduler scheduler)
    : m_storage(std::make_shared<Storage>(std::move(context),
                                          std::move(scheduler))) {}

ViewModelTaskScope::~ViewModelTaskScope() {
  if (!m_storage) {
    return;
  }
  m_storage->alive = false;
  CancelStorage(m_storage);
  m_storage->drainedObserver = nullptr;
  for (const auto &run : m_storage->runs) {
    run->observer = nullptr;
  }
}

auto ViewModelTaskScope::Start(Work work, Observer observer)
    -> ViewModelTaskHandle {
  return StartStorage(m_storage, std::move(work), std::move(observer));
}

auto ViewModelTaskScope::StartStorage(const std::shared_ptr<Storage> &storage,
                                      Work work, Observer observer)
    -> ViewModelTaskHandle {
  if (!storage || !storage->alive || !storage->acceptsWork || !work) {
    return {};
  }
  const auto id = storage->nextTaskId++;
  auto context = storage->context;
  auto run = std::make_shared<Run>(id, std::move(context), std::move(work),
                                   std::move(observer));
  run->context.BindLinkedCancellationToken(run->cancellation.GetToken());
  storage->runs.push_back(run);

  auto status = storage->status.Get();
  status.activeCount = storage->runs.size();
  status.startedCount += 1;
  status.lastOutcome = {};
  static_cast<void>(storage->status.Set(std::move(status)));

  NGIN::Async::Detach(run->context, ObserveRun(storage, run));
  const auto weak = std::weak_ptr<Run>{run};
  return ViewModelTaskHandle{
      id,
      [weak] {
        if (const auto current = weak.lock(); current && current->running) {
          current->cancellation.Cancel();
        }
      },
      [weak] {
        const auto current = weak.lock();
        return current && current->running;
      },
  };
}

auto ViewModelTaskScope::ObserveRun(std::shared_ptr<Storage> storage,
                                    std::shared_ptr<Run> run)
    -> NGIN::Async::Task<void> {
  auto outcome = ViewModelTaskOutcome{
      .taskId = run->id,
      .kind = ViewModelTaskOutcomeKind::Succeeded,
  };
#if NGIN_ASYNC_HAS_EXCEPTIONS
  try {
#endif
    auto operation = NGIN::Async::Spawn(run->context, run->work(run->context));
    auto completion = co_await operation;
    if (completion.IsDomainError()) {
      outcome.kind = ViewModelTaskOutcomeKind::DomainError;
      outcome.error = std::move(completion).DomainError();
    } else if (completion.IsCanceled()) {
      outcome.kind = ViewModelTaskOutcomeKind::Canceled;
    } else if (completion.IsFault()) {
      outcome.kind = ViewModelTaskOutcomeKind::Fault;
      outcome.error = FaultError(completion.Fault());
    }
#if NGIN_ASYNC_HAS_EXCEPTIONS
  } catch (...) {
    outcome.kind = ViewModelTaskOutcomeKind::Fault;
    outcome.error = ExceptionError();
  }
#endif
  FinishRun(storage, run, std::move(outcome));
}

void ViewModelTaskScope::FinishRun(const std::shared_ptr<Storage> &storage,
                                   const std::shared_ptr<Run> &run,
                                   ViewModelTaskOutcome outcome) {
  if (!run->running) {
    return;
  }
  run->running = false;
  std::erase(storage->runs, run);
  const auto drained = storage->runs.empty() && !storage->acceptsWork;
  auto drainedObserver =
      drained ? std::move(storage->drainedObserver) : DrainedObserver{};
  if (!storage->alive) {
    run->work = nullptr;
    run->observer = nullptr;
    if (drainedObserver) {
      drainedObserver();
    }
    return;
  }

  auto observer = std::move(run->observer);
  run->work = nullptr;
  run->observer = nullptr;

  auto status = storage->status.Get();
  status.activeCount = storage->runs.size();
  status.lastOutcome = outcome;
  switch (outcome.kind) {
  case ViewModelTaskOutcomeKind::Succeeded:
    status.succeededCount += 1;
    break;
  case ViewModelTaskOutcomeKind::Canceled:
    status.canceledCount += 1;
    break;
  case ViewModelTaskOutcomeKind::DomainError:
  case ViewModelTaskOutcomeKind::Fault:
    status.failedCount += 1;
    break;
  case ViewModelTaskOutcomeKind::None:
    break;
  }
  static_cast<void>(storage->status.Set(std::move(status)));
  if (observer) {
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      observer(outcome);
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      auto observerFailure = storage->status.Get();
      observerFailure.failedCount += 1;
      observerFailure.lastOutcome = ViewModelTaskOutcome{
          .taskId = run->id,
          .kind = ViewModelTaskOutcomeKind::Fault,
          .error = ObserverExceptionError(),
      };
      static_cast<void>(storage->status.Set(std::move(observerFailure)));
    }
#endif
  }
  if (drainedObserver) {
    drainedObserver();
  }
}

void ViewModelTaskScope::CancelAll() noexcept { CancelStorage(m_storage); }

void ViewModelTaskScope::Close(DrainedObserver observer) noexcept {
  if (!m_storage) {
    if (observer) {
      observer();
    }
    return;
  }
  CancelStorage(m_storage);
  if (m_storage->runs.empty()) {
    if (observer) {
      observer();
    }
    return;
  }
  m_storage->drainedObserver = std::move(observer);
}

auto ViewModelTaskScope::IsDrained() const noexcept -> bool {
  return !m_storage || m_storage->runs.empty();
}

void ViewModelTaskScope::CancelStorage(
    const std::shared_ptr<Storage> &storage) noexcept {
  if (!storage || !storage->acceptsWork) {
    return;
  }
  storage->acceptsWork = false;
  storage->cancellation.Cancel();
  for (const auto &run : storage->runs) {
    run->cancellation.Cancel();
  }
  auto status = storage->status.Get();
  status.acceptsWork = false;
  static_cast<void>(storage->status.Set(std::move(status)));
}

auto ViewModelTaskScope::Status() const -> const ViewModelTaskStatus & {
  static const ViewModelTaskStatus expired{.acceptsWork = false};
  return m_storage ? m_storage->status.Get() : expired;
}

auto ViewModelTaskScope::StatusBinding() const
    -> ReadOnlyBinding<ViewModelTaskStatus> {
  return m_storage ? m_storage->status.AsReadOnly()
                   : ReadOnlyBinding<ViewModelTaskStatus>{};
}
} // namespace NGIN::UI
