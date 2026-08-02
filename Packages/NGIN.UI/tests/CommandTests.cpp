#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/Completion.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <vector>

namespace {
[[nodiscard]] auto CompleteAfterYield(NGIN::Async::TaskContext &context,
                                      int &executions)
    -> NGIN::Async::Task<void, NGIN::UI::CommandError> {
  co_await context.YieldNow();
  ++executions;
}

[[nodiscard]] auto FailWithDomainError(NGIN::Async::TaskContext &)
    -> NGIN::Async::Task<void, NGIN::UI::CommandError> {
  co_await NGIN::Async::DomainFailure(NGIN::UI::CommandError{
      .kind = NGIN::UI::CommandErrorKind::Domain,
      .code = NGIN::Text::String{"save-rejected"},
      .message = NGIN::Text::String{"The item could not be saved"},
  });
}
} // namespace

TEST_CASE(
    "synchronous commands expose availability outcome and subscriptions") {
  using namespace NGIN::UI;

  int executions = 0;
  std::vector<CommandStatus> observed;
  Command command{[&] { ++executions; }, false};
  auto subscription = command.Subscribe(
      [&](const CommandStatus &status) { observed.push_back(status); });

  REQUIRE(command.Execute() == CommandInvocation::RejectedDisabled);
  command.SetEnabled(true);
  REQUIRE(command.Status().canExecute);
  REQUIRE(command.Execute() == CommandInvocation::Started);
  REQUIRE(executions == 1);
  REQUIRE_FALSE(command.Status().isRunning);
  REQUIRE(command.Status().lastOutcome.kind == CommandOutcomeKind::Succeeded);
  REQUIRE(observed.size() >= 3);

  command.SetEnabled(false);
  REQUIRE_FALSE(command.AsBinding().CanExecute());
}

TEST_CASE("synchronous command domain errors and exceptions are observable") {
  using namespace NGIN::UI;

  Command rejected{Command::Action{[]() -> CommandResult<void> {
    return CommandError{
        .kind = CommandErrorKind::Domain,
        .code = NGIN::Text::String{"invalid"},
        .message = NGIN::Text::String{"Input is invalid"},
    };
  }}};
  REQUIRE(rejected.Execute() == CommandInvocation::Started);
  REQUIRE(rejected.Status().lastOutcome.kind ==
          CommandOutcomeKind::DomainError);
  REQUIRE(rejected.Status().lastOutcome.error.has_value());
  CHECK(rejected.Status().lastOutcome.error->code ==
        NGIN::Text::String{"invalid"});

#if NGIN_ASYNC_HAS_EXCEPTIONS
  Command faulted{[] { throw 42; }};
  REQUIRE(faulted.Execute() == CommandInvocation::Started);
  REQUIRE(faulted.Status().lastOutcome.kind == CommandOutcomeKind::Fault);
  REQUIRE(faulted.Status().lastOutcome.error.has_value());
#endif
}

TEST_CASE("command bindings expire safely and drive composer button state") {
  using namespace NGIN::UI;

  int executions = 0;
  Command command{[&] { ++executions; }, false};

  Composer disabled;
  disabled.Button(command.AsBinding(), {}, "save");
  REQUIRE(disabled.Declarations().size() == 1);
  const auto &disabledButton = disabled.Declarations().front();
  REQUIRE_FALSE(disabledButton.properties.interaction.enabled);

  command.SetEnabled(true);
  Composer enabled;
  enabled.Button(command.AsBinding(), {}, "save");
  const auto &enabledButton = enabled.Declarations().front();
  REQUIRE(enabledButton.properties.interaction.enabled);
  enabledButton.properties.interaction.onActivate();
  REQUIRE(executions == 1);

  CommandBinding expired;
  {
    Command temporary{[] {}};
    expired = temporary.AsBinding();
    REQUIRE(expired.CanExecute());
  }
  REQUIRE_FALSE(expired.CanExecute());
  REQUIRE(expired.Execute() == CommandInvocation::RejectedExpired);
}

TEST_CASE("async commands complete on their task context and publish state") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  int executions = 0;
  AsyncCommand command{
      context,
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, executions);
      },
  };

  REQUIRE(command.Execute() == CommandInvocation::Started);
  REQUIRE(command.Status().isRunning);
  REQUIRE(command.Status().canCancel);
  REQUIRE_FALSE(command.Status().canExecute);

  scheduler.RunUntilIdle();
  REQUIRE(executions == 1);
  REQUIRE_FALSE(command.Status().isRunning);
  REQUIRE(command.Status().canExecute);
  REQUIRE(command.Status().lastOutcome.kind == CommandOutcomeKind::Succeeded);
}

TEST_CASE("async command exposes domain failure and cancellation") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  AsyncCommand rejected{context, FailWithDomainError};
  REQUIRE(rejected.Execute() == CommandInvocation::Started);
  scheduler.RunUntilIdle();
  REQUIRE(rejected.Status().lastOutcome.kind ==
          CommandOutcomeKind::DomainError);
  REQUIRE(rejected.Status().lastOutcome.error.has_value());
  CHECK(rejected.Status().lastOutcome.error->code ==
        NGIN::Text::String{"save-rejected"});

  int executions = 0;
  AsyncCommand canceled{
      context,
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, executions);
      },
  };
  REQUIRE(canceled.Execute() == CommandInvocation::Started);
  canceled.Cancel();
  scheduler.RunUntilIdle();
  REQUIRE(executions == 0);
  REQUIRE(canceled.Status().lastOutcome.kind == CommandOutcomeKind::Canceled);
}

TEST_CASE("async command concurrency policies are deterministic") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  int rejectedExecutions = 0;
  AsyncCommand reject{
      context,
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, rejectedExecutions);
      },
  };
  REQUIRE(reject.Execute() == CommandInvocation::Started);
  REQUIRE(reject.Execute() == CommandInvocation::RejectedRunning);
  scheduler.RunUntilIdle();
  REQUIRE(rejectedExecutions == 1);

  int queuedExecutions = 0;
  AsyncCommand queue{
      context,
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, queuedExecutions);
      },
      true,
      CommandConcurrencyPolicy::Queue,
      1,
  };
  REQUIRE(queue.Execute() == CommandInvocation::Started);
  REQUIRE(queue.Execute() == CommandInvocation::Queued);
  REQUIRE(queue.Execute() == CommandInvocation::RejectedQueueFull);
  REQUIRE(queue.Status().queuedCount == 1);
  scheduler.RunUntilIdle();
  REQUIRE(queuedExecutions == 2);
  REQUIRE_FALSE(queue.Status().isRunning);

  int replacementExecutions = 0;
  AsyncCommand replace{
      context,
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, replacementExecutions);
      },
      true,
      CommandConcurrencyPolicy::CancelPrevious,
  };
  REQUIRE(replace.Execute() == CommandInvocation::Started);
  REQUIRE(replace.Execute() == CommandInvocation::Replaced);
  scheduler.RunUntilIdle();
  REQUIRE(replacementExecutions == 1);
  REQUIRE(replace.Status().lastOutcome.kind == CommandOutcomeKind::Succeeded);
}

TEST_CASE("destroying an async command expires bindings and cancels work") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  int executions = 0;
  CommandBinding binding;
  {
    AsyncCommand command{
        context,
        [&](NGIN::Async::TaskContext &runContext) {
          return CompleteAfterYield(runContext, executions);
        },
    };
    binding = command.AsBinding();
    REQUIRE(binding.Execute() == CommandInvocation::Started);
  }

  scheduler.RunUntilIdle();
  REQUIRE(executions == 0);
  REQUIRE_FALSE(binding.CanExecute());
  REQUIRE(binding.Execute() == CommandInvocation::RejectedExpired);
}

TEST_CASE("window and application lifetime cancel asynchronous commands") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"CommandLifetime"},
      .title = NGIN::Text::String{"Command lifetime"},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();
  int executions = 0;
  auto command = std::make_unique<AsyncCommand>(
      application->CreateTaskContext(*window),
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, executions);
      });

  REQUIRE(command->Execute() == CommandInvocation::Started);
  REQUIRE(application->CloseWindow(*window).HasValue());
  for (auto index = 0; index < 8 && command->Status().isRunning; ++index) {
    REQUIRE(application->PumpOnce().HasValue());
  }
  REQUIRE(executions == 0);
  REQUIRE(command->Status().lastOutcome.kind == CommandOutcomeKind::Canceled);

  auto secondWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"ApplicationLifetime"},
      .title = NGIN::Text::String{"Application lifetime"},
  });
  REQUIRE(secondWindow.HasValue());
  command = std::make_unique<AsyncCommand>(
      application->CreateTaskContext(*secondWindow.Value()),
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, executions);
      });
  REQUIRE(command->Execute() == CommandInvocation::Started);
  application.reset();
  REQUIRE(executions == 0);
  REQUIRE(command->Status().lastOutcome.kind == CommandOutcomeKind::Canceled);
}
