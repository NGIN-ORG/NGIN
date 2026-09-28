#include "TaskOwnerFixture.hpp"
#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/Validation.hpp>
#include <NGIN/UI/ViewModel.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
auto Work(NGIN::Async::TaskContext &context, int &count)
    -> NGIN::Async::Task<void, NGIN::UI::CommandError> {
  co_await context.YieldNow();
  ++count;
}
auto Validate(NGIN::Async::TaskContext &context, int value)
    -> NGIN::Async::Task<std::vector<NGIN::UI::ValidationIssue>,
                         NGIN::UI::ValidationIssue> {
  co_await context.YieldNow();
  if (value < 0)
    co_return std::vector{
        NGIN::UI::ValidationIssue{.id = NGIN::Text::String{"negative"}}};
  co_return std::vector<NGIN::UI::ValidationIssue>{};
}
} // namespace

TEST_CASE("UI owners retain command and ViewModel work through completion") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  int count = 0;
  AsyncCommand command{tasks.owner, context,
                       [&](auto &ctx) { return Work(ctx, count); }};
  ViewModelTaskScope scope{tasks.owner, context};
  REQUIRE(command.Execute() == CommandInvocation::Started);
  auto handle = scope.Start([&](auto &ctx) { return Work(ctx, count); });
  REQUIRE(handle);
  REQUIRE(tasks.owner.Pending() == 2);
  scheduler.RunUntilIdle();
  CHECK(count == 2);
  CHECK(command.Status().lastOutcome.kind == CommandOutcomeKind::Succeeded);
  CHECK(scope.Status().succeededCount == 1);
  REQUIRE(tasks.owner.TryJoin());
}

TEST_CASE("UI owner shutdown cancels runs and clears running status") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  int count = 0;
  AsyncCommand command{tasks.owner, context,
                       [&](auto &ctx) { return Work(ctx, count); }};
  ViewModelTaskScope scope{tasks.owner, context};
  REQUIRE(command.Execute() == CommandInvocation::Started);
  REQUIRE(scope.Start([&](auto &ctx) { return Work(ctx, count); }));
  tasks.owner.Close();
  tasks.owner.RequestStop(NGIN::Async::StopReason::RuntimeShutdown);
  scheduler.RunUntilIdle();
  CHECK(count == 0);
  CHECK_FALSE(command.Status().isRunning);
  CHECK(command.Status().lastOutcome.kind == CommandOutcomeKind::Canceled);
  CHECK(scope.Status().canceledCount == 1);
  REQUIRE(tasks.owner.TryJoin());
}

TEST_CASE("UI starters expose closed and exhausted owner admission") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler, 1};
  NGIN::Async::TaskContext context{scheduler};
  int count = 0;
  AsyncCommand first{tasks.owner, context,
                     [&](auto &ctx) { return Work(ctx, count); }};
  AsyncCommand rejected{tasks.owner, context,
                        [&](auto &ctx) { return Work(ctx, count); }};
  ViewModelTaskScope scope{tasks.owner, context};
  State<int> input{1};
  ValidationField field{input.AsReadOnly(), ValidationTrigger::Deferred};
  field.SetAsyncValidator(tasks.owner, context, Validate);
  REQUIRE(first.Execute() == CommandInvocation::Started);
  CHECK(rejected.Execute() == CommandInvocation::RejectedOwner);
  CHECK_FALSE(rejected.Status().isRunning);
  CHECK(rejected.Status().lastOutcome.kind == CommandOutcomeKind::Fault);
  CHECK_FALSE(scope.Start([&](auto &ctx) { return Work(ctx, count); }));
  CHECK(scope.Status().activeCount == 0);
  CHECK(scope.Status().failedCount == 1);
  field.Validate();
  CHECK_FALSE(field.IsValidating().Get());
  REQUIRE(field.Issues().Get().size() == 1);
  CHECK(field.Issues().Get()[0].id ==
        NGIN::Text::String{"task-admission-rejected"});
  scheduler.RunUntilIdle();
  CHECK(count == 1);
  REQUIRE(tasks.owner.TryJoin());
  CHECK(first.Execute() == CommandInvocation::RejectedOwner);
}

TEST_CASE("destroying queued UI starters preserves owner cleanup") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  int count = 0;
  State<int> input{1};
  {
    AsyncCommand command{tasks.owner, context,
                         [&](auto &ctx) { return Work(ctx, count); }};
    ViewModelTaskScope scope{tasks.owner, context};
    ValidationField field{input.AsReadOnly()};
    field.SetAsyncValidator(tasks.owner, context, Validate);
    REQUIRE(command.Execute() == CommandInvocation::Started);
    REQUIRE(scope.Start([&](auto &ctx) { return Work(ctx, count); }));
    REQUIRE(tasks.owner.Pending() == 3);
  }
  scheduler.RunUntilIdle();
  CHECK(count == 0);
  auto report = tasks.owner.TryJoin();
  REQUIRE(report);
  CHECK_FALSE(report->HasError());
}

TEST_CASE("UI command concurrency remains ordered under supervision") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  int count = 0;
  AsyncCommand command{tasks.owner,
                       context,
                       [&](auto &ctx) { return Work(ctx, count); },
                       true,
                       CommandConcurrencyPolicy::Queue,
                       2};
  REQUIRE(command.Execute() == CommandInvocation::Started);
  REQUIRE(command.Execute() == CommandInvocation::Queued);
  REQUIRE(command.Execute() == CommandInvocation::Queued);
  scheduler.RunUntilIdle();
  CHECK(count == 3);
  CHECK_FALSE(command.Status().isRunning);
  REQUIRE(tasks.owner.TryJoin());
}

TEST_CASE("supervised commands retain mutable callable identity across runs") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  int observed = 0;
  AsyncCommand command{
      tasks.owner, context,
      [calls = 0, &observed](NGIN::Async::TaskContext &) mutable {
        observed = ++calls;
        return NGIN::Async::Task<void, CommandError>::FromValue();
      }};
  REQUIRE(command.Execute() == CommandInvocation::Started);
  scheduler.RunUntilIdle();
  REQUIRE(command.Execute() == CommandInvocation::Started);
  scheduler.RunUntilIdle();
  CHECK(observed == 2);
  REQUIRE(tasks.owner.TryJoin());
}

namespace {
struct HeldWork {
  std::coroutine_handle<> continuation{};
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) noexcept {
    continuation = handle;
  }
  void await_resume() const noexcept {}
  void Open(NGIN::Execution::CooperativeScheduler &scheduler) {
    REQUIRE(continuation);
    REQUIRE(scheduler.Execute(
        NGIN::Execution::WorkItem(std::exchange(continuation, {}))));
  }
};
auto LateCommandError(HeldWork &hold)
    -> NGIN::Async::Task<void, NGIN::UI::CommandError> {
  co_await hold;
  co_await NGIN::Async::DomainFailure(
      NGIN::UI::CommandError{.code = NGIN::Text::String{"late-command"},
                             .message = NGIN::Text::String{"save failed"}});
}
auto LateFault(HeldWork &hold)
    -> NGIN::Async::Task<void, NGIN::UI::CommandError> {
  co_await hold;
  co_await NGIN::Async::Faulted(NGIN::Async::MakeAsyncFault(
      NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, 42, "late fault"));
}
auto LateValidationError(HeldWork &hold)
    -> NGIN::Async::Task<std::vector<NGIN::UI::ValidationIssue>,
                         NGIN::UI::ValidationIssue> {
  co_await hold;
  co_return NGIN::UI::ValidationIssue{
      .id = NGIN::Text::String{"late-validation"},
      .message = NGIN::Text::String{"lookup failed"}};
}
} // namespace

TEST_CASE("late errors survive destruction of their UI observers") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  HeldWork commandHold, viewModelHold, validationHold;
  State<int> input{1};
  {
    AsyncCommand command{tasks.owner, context,
                         [&](auto &) { return LateCommandError(commandHold); }};
    ViewModelTaskScope scope{tasks.owner, context};
    ValidationField field{input.AsReadOnly()};
    field.SetAsyncValidator(tasks.owner, context, [&](auto &, int) {
      return LateValidationError(validationHold);
    });
    REQUIRE(command.Execute() == CommandInvocation::Started);
    REQUIRE(scope.Start([&](auto &) { return LateFault(viewModelHold); }));
    scheduler.RunUntilIdle();
  }
  commandHold.Open(scheduler);
  viewModelHold.Open(scheduler);
  validationHold.Open(scheduler);
  scheduler.RunUntilIdle();
  auto report = tasks.owner.TryJoin();
  REQUIRE(report);
  REQUIRE(report->Errors().size() == 3);
  // Validation was admitted first; diagnostics retain admission order.
  CHECK(report->Errors()[0].Fault().message ==
        "late-validation: lookup failed");
  CHECK(report->Errors()[1].Fault().message == "late-command: save failed");
  CHECK(report->Errors()[2].Fault().code ==
        NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed);
  CHECK(report->Errors()[2].Fault().native == 42);
}

TEST_CASE(
    "superseded validation reports late failure without changing new state") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  HeldWork hold;
  State<int> input{1};
  ValidationField field{input.AsReadOnly()};
  field.SetAsyncValidator(tasks.owner, context, [&](auto &, int value) {
    if (value == 1)
      return LateValidationError(hold);
    return NGIN::Async::Task<std::vector<ValidationIssue>,
                             ValidationIssue>::FromValue({});
  });
  scheduler.RunUntilIdle();
  static_cast<void>(input.Set(2));
  scheduler.RunUntilIdle();
  REQUIRE(field.IsValid().Get());
  hold.Open(scheduler);
  scheduler.RunUntilIdle();
  CHECK(field.IsValid().Get());
  CHECK(field.Issues().Get().empty());
  auto report = tasks.owner.TryJoin();
  REQUIRE(report);
  REQUIRE(report->HasError());
  CHECK(report->Error().Fault().message == "late-validation: lookup failed");
}

TEST_CASE("owner shutdown leaves interrupted validation unavailable") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  NGIN::Async::TaskContext context{scheduler};
  State<int> input{1};
  ValidationField field{input.AsReadOnly()};
  field.SetAsyncValidator(tasks.owner, context, Validate);
  tasks.owner.RequestStop(NGIN::Async::StopReason::RuntimeShutdown);
  scheduler.RunUntilIdle();
  CHECK_FALSE(field.IsValidating().Get());
  CHECK_FALSE(field.IsValid().Get());
  CHECK(field.Issues().Get().empty());
  REQUIRE(tasks.owner.TryJoin());
}

#if NGIN_ASYNC_HAS_EXCEPTIONS
namespace {
struct RejectingResource final : std::pmr::memory_resource {
  void *do_allocate(std::size_t, std::size_t) override {
    throw std::bad_alloc{};
  }
  void do_deallocate(void *, std::size_t, std::size_t) override {}
  bool
  do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
    return this == &other;
  }
};
} // namespace
TEST_CASE("UI setup failure publishes no running work") {
  using namespace NGIN::UI;
  NGIN::Execution::CooperativeScheduler scheduler;
  TaskOwnerFixture tasks{scheduler};
  RejectingResource resource;
  NGIN::Async::TaskContext context{NGIN::Async::AsyncEnvironment{
      .executor = NGIN::Execution::ExecutorRef::From(scheduler),
      .resource = &resource}};
  int count = 0;
  AsyncCommand command{tasks.owner, context,
                       [&](auto &ctx) { return Work(ctx, count); }};
  ViewModelTaskScope scope{tasks.owner, context};
  State<int> input{1};
  ValidationField field{input.AsReadOnly(), ValidationTrigger::Deferred};
  field.SetAsyncValidator(tasks.owner, context, Validate);
  REQUIRE_THROWS_AS(command.Execute(), std::bad_alloc);
  CHECK_FALSE(command.Status().isRunning);
  REQUIRE_THROWS_AS(scope.Start([&](auto &ctx) { return Work(ctx, count); }),
                    std::bad_alloc);
  CHECK(scope.Status().activeCount == 0);
  REQUIRE_THROWS_AS(field.Validate(), std::bad_alloc);
  CHECK_FALSE(field.IsValidating().Get());
  CHECK(tasks.owner.Pending() == 0);
}
#endif
