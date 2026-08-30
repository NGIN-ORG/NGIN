---
title: Learn NGIN.Execution
description: Start with a caller-driven scheduler, then choose execution ownership, submit work, use threads and fibers, and shut down safely.
---

# Learn NGIN.Execution

NGIN.Execution is the runtime layer that decides **where and when work runs**.
It provides schedulers, a non-owning executor reference, move-only work items,
native thread ownership, and optional stackful fibers. NGIN.Async builds on this
layer, but Execution is also usable by ordinary callable-based code.

If you have not used it before, start here. The most important design rule is
that NGIN never installs a hidden scheduler: your application creates one,
drives or owns it, and shuts it down.

## What you can build

- deterministic caller-driven queues for tests, tools, and application loops;
- worker-thread pools for parallel and delayed work;
- immediate inline execution where re-entrancy is acceptable;
- stackful cooperative jobs on fiber-enabled platforms;
- native worker threads with explicit join/detach/destruction policy;
- runtime-independent code that accepts an `ExecutorRef` instead of a concrete
  scheduler type.

## The four objects to understand

| Object | What it represents | Ownership |
| --- | --- | --- |
| Scheduler | A concrete queue and execution policy | Created and owned by your application/runtime |
| `ExecutorRef` | A borrowed type-erased view of a scheduler | Never owns or extends scheduler lifetime |
| `WorkItem` | One move-only callable or coroutine handle | Consumed by successful submission |
| `ScheduleResult` | Whether submission was accepted | Returned immediately to the submitter |

The normal callable flow is:

```text
callable → WorkItem → scheduler.Execute(...) → accepted queue → Invoke()
                         │
                         └─ ScheduleResult (accepted or exact rejection)
```

An accepted submission transfers the work item to the scheduler. Acceptance
does not mean the callback has already run, except for `InlineScheduler`.

## Your first scheduler

`CooperativeScheduler` is the clearest place to begin because the calling
thread controls every execution step:

```cpp
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>

#include <iostream>

int main() {
    NGIN::Execution::CooperativeScheduler scheduler;
    auto executor = NGIN::Execution::ExecutorRef::From(scheduler);

    auto submitted = executor.Execute([] {
        std::cout << "work ran\n";
    });

    if (!submitted) {
        std::cerr << "submission failed\n";
        return 1;
    }

    std::cout << "work queued\n";
    scheduler.RunUntilIdle();
}
```

Expected output:

```text
work queued
work ran
```

Nothing runs between `Execute` and `RunUntilIdle`. The scheduler has no worker
thread; the owner must pump it. Read [your first scheduler](./execution/first-scheduler.md)
for one-step driving, delayed work, and the complete CMake setup.

## Choose the execution owner

| Scheduler | Who runs work? | Delayed work | Best fit |
| --- | --- | --- | --- |
| `InlineScheduler` | The submitting thread, before `Execute` returns | Sleeps until the deadline, then invokes | Adapters, tiny tests, deliberately synchronous paths |
| `CooperativeScheduler` | The thread calling `RunOne`/`RunUntilIdle` | Runs when the owner pumps at or after the deadline | Event loops, deterministic tests, tools |
| `ThreadPoolScheduler` | Owned workers; caller may also pump | Owned timer thread dispatches due work | Parallel background work and application runtimes |
| `FiberScheduler` | Owned worker threads running reusable fibers | Owned driver thread | Existing synchronous call chains that must cooperatively yield |

These are not interchangeable performance presets. They change re-entrancy,
thread affinity, progress, shutdown, and testing behavior. Read
[choosing a scheduler](./execution/choosing-scheduler.md) before selecting one
for a library boundary.

## Submission is fallible

Every scheduler returns `ScheduleResult`, an `std::expected<void,
ScheduleError>`:

```cpp
auto executor = NGIN::Execution::ExecutorRef::From(scheduler);
auto result = executor.Execute([] { DoWork(); });
if (!result) {
    switch (result.error()) {
    case NGIN::Execution::ScheduleError::InvalidExecutor:
        break;
    case NGIN::Execution::ScheduleError::Rejected:
        break;
    case NGIN::Execution::ScheduleError::Stopped:
        break;
    case NGIN::Execution::ScheduleError::ResourceExhausted:
        break;
    }
}
```

Do not ignore this result. If an async continuation cannot be scheduled, the
owning async operation converts that infrastructure failure into an
`AsyncFault`; direct Execution users must choose their own policy.

## Immediate and delayed submission

Concrete schedulers provide:

```cpp
scheduler.Execute(work);
scheduler.ExecuteAt(work, absoluteMonotonicTime);
```

`ExecutorRef` additionally offers `ExecuteAfter` for NGIN time quantities. A
deadline means **not before** a monotonic time point, not a real-time
guarantee. Queueing, contention, and pump cadence can make execution later.

Read [submitting work](./execution/submitting-work.md) for callable storage,
move semantics, delayed scheduling, error handling, and `ExecutorRef` lifetime.

## Threads and fibers are different tools

`Thread` is a move-only operating-system thread handle. It has an explicit
destruction policy: join, detach, or terminate. `WorkerThread` is the safer
joining wrapper for owned workers.

`Fiber` is a move-only stackful execution context that belongs to one thread.
It resumes until it yields, completes, or faults. Fiber support can be disabled
at build time, and a fiber is not a substitute for an OS thread.

Read [threads and fibers](./execution/threads-fibers.md) for ownership,
platform availability, stack allocation, exception capture, and the boundary
between these abstractions.

## Relationship to NGIN.Async

`TaskContext` stores an `ExecutorRef`. Async operations schedule their
continuations through that reference:

```cpp
NGIN::Execution::ThreadPoolScheduler scheduler {4};
NGIN::Async::TaskContext context {scheduler};
auto operation = NGIN::Async::Spawn(context, Load(context));
```

The scheduler must outlive the context and all work that can use it. A task
does not become thread-safe merely because it runs on a thread pool; captured
state still needs its own synchronization or confinement policy.

## Shutdown order

The safe general sequence is:

1. stop new producers;
2. request cancellation for async work;
3. drive or wait for owned operations to become terminal;
4. release operation/result owners;
5. destroy contexts and borrowed executor references;
6. destroy the scheduler and join its worker threads.

`CancelAll` discards queued/timed work. It does not interrupt a callback that
is already running. Read [shutdown and lifetimes](./execution/shutdown-lifetimes.md)
before putting a scheduler in a service or plugin.

## Common beginner problems

| Symptom | Cause | Fix |
| --- | --- | --- |
| Queued work never runs | A cooperative scheduler is not being pumped | Call `RunOne`/`RunUntilIdle` from the owning loop |
| Callback runs before surrounding code expects | An inline scheduler is re-entrant | Use a queued scheduler or make the call site re-entrancy-safe |
| Submission disappears during shutdown | `ScheduleResult` was ignored | Handle `Stopped`/`Rejected` and stop producers first |
| Crash through `ExecutorRef` | Its concrete scheduler was already destroyed | Make the scheduler outlive every reference and queued continuation |
| Process terminates when a `Thread` dies | Default destruction policy found a joinable thread | Join explicitly or use `WorkerThread`/a chosen policy |
| Fiber API does not compile | Stackful fibers are disabled for the build/platform | Check `NGIN_EXECUTION_HAS_STACKFUL_FIBERS` and choose another model |

## Continue

### Learn in order

1. [Your first scheduler](./execution/first-scheduler.md)
2. [Choosing a scheduler](./execution/choosing-scheduler.md)
3. [Submitting work](./execution/submitting-work.md)
4. [Threads and fibers](./execution/threads-fibers.md)
5. [Shutdown and lifetimes](./execution/shutdown-lifetimes.md)

### Look up exact code

- [Execution symbol index](../../reference/cpp/base/execution.md)
- [`ExecutorRef`](../../reference/cpp/base/execution/executor-ref.md)
- [Scheduler classes](../../reference/cpp/base/execution/schedulers.md)
- [`WorkItem` and scheduling errors](../../reference/cpp/base/execution/work-item.md)
- [`Thread` and `WorkerThread`](../../reference/cpp/base/execution/thread.md)
- [`Fiber`](../../reference/cpp/base/execution/fiber.md)
