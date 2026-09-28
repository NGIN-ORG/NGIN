#include "../src/NGIN/UI/UIAsyncScheduler.hpp"
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Async/WithTimeout.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace NGIN::Async;
using NGIN::UI::Detail::UIAsyncScheduler;
struct Platform {
  std::chrono::nanoseconds now{std::chrono::seconds(5)};
  unsigned wakes{};
  auto MonotonicNow() const noexcept { return now; }
  void WakeEventLoop() noexcept { ++wakes; }
};
Task<void> Waiting() {
  TaskContext context(co_await CurrentEnvironment());
  co_await context.Delay(NGIN::Units::Seconds(60));
}
} // namespace

TEST_CASE("UI scheduler enqueues work and rejects after shutdown without "
          "inline fallback",
          "[Async][UI]") {
  Platform platform;
  UIAsyncScheduler scheduler(1);
  scheduler.Bind(platform);
  bool invoked = false, current = false;
  REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem([&] {
    invoked = true;
    current = scheduler.IsCurrent();
  })));
  auto full = scheduler.Execute(NGIN::Execution::WorkItem([] {}));
  REQUIRE_FALSE(full);
  REQUIRE(full.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
  REQUIRE_FALSE(invoked);
  scheduler.RunReady(platform.now);
  REQUIRE(invoked);
  REQUIRE(current);
  REQUIRE_FALSE(scheduler.IsCurrent());
  scheduler.Shutdown();
  invoked = false;
  auto stopped =
      scheduler.Execute(NGIN::Execution::WorkItem([&] { invoked = true; }));
  REQUIRE_FALSE(stopped);
  REQUIRE(stopped.error() == NGIN::Execution::ScheduleError::Stopped);
  REQUIRE_FALSE(invoked);
}

TEST_CASE("UI reserved completion remains deliverable after ordinary admission "
          "closes",
          "[Async][UI]") {
  Platform platform;
  UIAsyncScheduler scheduler(1);
  scheduler.Bind(platform);
  bool invoked = false;
  auto reserved = scheduler.ReserveCompletion(
      NGIN::Execution::WorkItem([&] { invoked = true; }));
  REQUIRE(reserved);
  scheduler.Shutdown();
  reserved->Schedule();
  scheduler.RunReady(platform.now);
  REQUIRE(invoked);
  reserved->Reset();
  REQUIRE(platform.wakes > 0);
}

TEST_CASE(
    "UI timer cancellation releases callback captures outside its queue lock",
    "[Async][UI]") {
  Platform platform;
  UIAsyncScheduler scheduler;
  scheduler.Bind(platform);
  bool released = false;
  struct Probe {
    UIAsyncScheduler &scheduler;
    bool &released;
    ~Probe() {
      static_cast<void>(scheduler.Execute(
          NGIN::Execution::WorkItem([&flag = released] { flag = true; })));
    }
  };
  auto probe = std::make_shared<Probe>(scheduler, released);
  auto timer = scheduler.ScheduleTimer(NGIN::Execution::WorkItem([probe] {}),
                                       detail::DeadlineAfter(1e9).value());
  REQUIRE(timer);
  probe.reset();
  REQUIRE(scheduler.NextDeadline());
  REQUIRE(*scheduler.NextDeadline() > platform.now);
  REQUIRE(timer->Cancel());
  REQUIRE_FALSE(released);
  scheduler.RunReady(platform.now);
  REQUIRE(released);
  REQUIRE_FALSE(scheduler.NextDeadline());
}

TEST_CASE("UI dispatch batches are bounded and due timers share progress",
          "[Async][UI]") {
  Platform platform;
  UIAsyncScheduler scheduler;
  scheduler.Bind(platform);
  int calls = 0;
  for (int index = 0; index != 128; ++index)
    REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem([&] { ++calls; })));
  auto timer = scheduler.ScheduleTimer(
      NGIN::Execution::WorkItem([&] { ++calls; }), NGIN::Time::TimePoint{});
  REQUIRE(timer);
  scheduler.RunReady(platform.now);
  REQUIRE(calls == 64);
  scheduler.DrainReady(platform.now);
  REQUIRE(calls == 129);
  REQUIRE_FALSE(scheduler.NextDeadline());
}

TEST_CASE("UI supervision joins canceled work using reserved delivery",
          "[Async][UI]") {
  Platform platform;
  UIAsyncScheduler scheduler;
  scheduler.Bind(platform);
  TaskContext context(scheduler);
  TaskSupervisor owner(context.GetEnvironment(), 1);
  REQUIRE(owner.Transfer(Waiting()));
  scheduler.RunReady(platform.now);
  REQUIRE(owner.Pending() == 1);
  owner.Close();
  owner.RequestStop(StopReason::RuntimeShutdown);
  REQUIRE_FALSE(owner.TryJoin());
  scheduler.DrainReady(platform.now);
  auto report = owner.TryJoin();
  REQUIRE(report);
  REQUIRE(report->IsStopped());
  REQUIRE(report->StopReason() == StopReason::RuntimeShutdown);
  REQUIRE_FALSE(scheduler.NextDeadline());
  scheduler.Shutdown();
}

TEST_CASE("UI deadlines use the platform clock and retire their timers",
          "[Async][UI]") {
  Platform platform;
  UIAsyncScheduler scheduler;
  scheduler.Bind(platform);
  TaskContext context(scheduler);
  TaskSupervisor owner(context.GetEnvironment(), 1);
  auto operation =
      owner.Spawn(WithTimeout(NGIN::Units::Seconds(1), Waiting())).value();
  scheduler.RunReady(platform.now);
  REQUIRE_FALSE(operation.IsCompleted());
  platform.now += std::chrono::seconds(2);
  scheduler.DrainReady(platform.now);
  auto result = operation.TakeResult();
  REQUIRE(result.IsStopped());
  REQUIRE(result.StopReason() == StopReason::Deadline);
  REQUIRE(owner.TryJoin());
  REQUIRE_FALSE(scheduler.NextDeadline());
}
