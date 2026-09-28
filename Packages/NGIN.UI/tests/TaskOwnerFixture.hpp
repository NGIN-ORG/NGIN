#pragma once
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

// Test-only cleanup also drains work when a REQUIRE unwinds the fixture.
struct TaskOwnerFixture final {
  explicit TaskOwnerFixture(NGIN::Execution::CooperativeScheduler &executor,
                            std::size_t capacity = 256)
      : scheduler(executor),
        owner(
            NGIN::Async::AsyncEnvironment{
                .executor = NGIN::Execution::ExecutorRef::From(executor)},
            capacity) {}
  ~TaskOwnerFixture() {
    owner.Close();
    owner.RequestStop(NGIN::Async::StopReason::RuntimeShutdown);
    scheduler.RunUntilIdle();
    if (!owner.TryJoin())
      std::terminate();
  }
  NGIN::Execution::CooperativeScheduler &scheduler;
  NGIN::Async::TaskSupervisor<> owner;
};
