---
title: Your first scheduler
description: Build and drive a complete NGIN.Execution program one queue operation at a time.
---

# Your first scheduler

This guide creates a deterministic, caller-driven scheduler. You will submit
immediate and delayed work, observe the queue, and decide when work runs.

## Build setup

```cmake
find_package(NGINBase CONFIG REQUIRED COMPONENTS Execution)
target_link_libraries(ExecutionDemo PRIVATE NGIN::Base::Execution)
target_compile_features(ExecutionDemo PRIVATE cxx_std_23)
```

## Complete program

```cpp
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Time/MonotonicClock.hpp>

#include <iostream>

int main() {
    using namespace NGIN;

    Execution::CooperativeScheduler scheduler;
    auto executor = Execution::ExecutorRef::From(scheduler);
    int total = 0;

    auto first = executor.Execute([&] {
        total += 2;
        std::cout << "first: " << total << '\n';
    });
    if (!first) {
        return 1;
    }

    const auto deadline = Time::TimePoint::FromNanoseconds(
        Time::MonotonicClock::Now().ToNanoseconds() + 1'000'000'000);
    auto later = executor.ExecuteAt([&] {
        total += 5;
        std::cout << "later: " << total << '\n';
    }, deadline);
    if (!later) {
        return 2;
    }

    std::cout << "queued: " << scheduler.PendingReady()
              << " ready, " << scheduler.PendingTimers()
              << " timer\n";

    scheduler.RunOne();
    scheduler.RunUntilIdleAt(deadline);
    return total == 7 ? 0 : 3;
}
```

Expected output:

```text
queued: 1 ready, 1 timer
first: 2
later: 7
```

## What the owner controls

`Execute` appends a ready item. `ExecuteAt` stores a timer ordered by monotonic
deadline. Neither creates a thread. `RunOne()` checks a due timer, then ready
work, and invokes at most one item. `RunUntilIdleAt(deadline)` uses the supplied
time point, which makes timer tests deterministic without sleeping.

The example checks every submission before driving the scheduler. Once
accepted, the scheduler owns the move-only work item. On rejection the caller
receives an error and no callback will run.

## Real application loop

A loop that also owns window or device events normally uses current time:

```cpp
while (!stopping) {
    PollPlatformEvents();
    while (scheduler.RunOne()) {
        // Drain work ready now.
    }
    WaitForPlatformEventOrNextDeadline();
}
```

`RunUntilIdle` does not wait for future timers. It drains work that is ready at
the instants it calls `RunOne`. The outer loop owns sleeping/waking policy.

## Next

Read [choosing a scheduler](./choosing-scheduler.md), then
[submitting work](./submitting-work.md).
