---
title: Submitting work
description: Understand WorkItem storage, ExecutorRef borrowing, deadlines, move semantics, and ScheduleResult failures.
---

# Submitting work

## Submit a callable

Every scheduler accepts a `WorkItem`; `ExecutorRef` convenience templates
construct one from a move-constructible `void()` callable:

```cpp
auto executor = NGIN::Execution::ExecutorRef::From(scheduler);
auto result = executor.Execute([state = std::move(state)]() mutable {
    Consume(std::move(state));
});
```

`WorkItem` is move-only. Small nothrow-movable callables are stored inline;
larger callables may allocate. If construction allocation fails, templated
`ExecutorRef` submission reports `ResourceExhausted`. Constructing a
`WorkItem` directly from an empty callable is invalid.

`Invoke()` is `noexcept`. A callable exception escaping a work item terminates
the process. Catch and translate expected exceptions inside the callable, or
use NGIN.Async’s fault/completion boundary where appropriate.

## Borrow through ExecutorRef

```cpp
NGIN::Execution::ThreadPoolScheduler scheduler {4};
auto executor = NGIN::Execution::ExecutorRef::From(scheduler);

auto result = executor.Execute([] { RefreshIndex(); });
```

`ExecutorRef` stores a raw state pointer and function pointers. It does not
allocate, own, reference-count, or stop the scheduler. Never return an executor
reference to a scope that destroys its scheduler.

A default-constructed executor is invalid and returns
`ScheduleError::InvalidExecutor`.

## Schedule a deadline

```cpp
auto absolute = executor.ExecuteAt(
    [] { ExpireEntry(); },
    NGIN::Time::TimePoint::FromNanoseconds(deadlineNs));

auto relative = executor.ExecuteAfter(
    [] { Retry(); },
    NGIN::Units::Milliseconds {250.0});
```

Relative delays are converted to an absolute monotonic time. Non-positive
delays become immediate submissions. Fractional nanoseconds round upward;
overflow saturates at the largest time point.

A successful delayed submission promises “not before the deadline.” It does
not promise a maximum lateness or a particular worker.

## Handle all failures

| Error | Meaning | Typical policy |
| --- | --- | --- |
| `InvalidExecutor` | `ExecutorRef` has no target | Configuration/programming failure |
| `Rejected` | Empty work or scheduler/policy rejection | Validate work; decide whether to run elsewhere |
| `Stopped` | Scheduler shutdown has begun | Stop producer and propagate cancellation/shutdown |
| `ResourceExhausted` | Queue or callable storage could not allocate | Apply backpressure, shed work, or report resource failure |

Submission errors happen before ownership transfer. Do not assume retry is
safe if the callable captured a one-shot external action; define retry at the
application operation level.
