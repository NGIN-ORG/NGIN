---
title: Async contexts and schedulers
description: Choose an executor, drive task progress, schedule delays, and shut the runtime down safely.
---

# Async contexts and schedulers

Task code describes asynchronous control flow. The `TaskContext` decides where
continuations run and which cancellation token they observe.

## Create a context

```cpp
NGIN::Execution::CooperativeScheduler scheduler;
NGIN::Async::TaskContext context {scheduler};
```

The context stores a non-owning `ExecutorRef`. It does not own `scheduler`.
Keep the concrete scheduler alive until every task using the context reaches a
terminal state.

## Choose a scheduler

| Scheduler | Who makes progress | Good fit |
| --- | --- | --- |
| `InlineScheduler` | Submission runs immediately | Small adapters/tests where re-entrancy is safe |
| `CooperativeScheduler` | Your thread calls `RunOne`/`RunUntilIdle` | Event loops, deterministic tests, tools |
| `ThreadPoolScheduler` | Scheduler-owned workers | Independent CPU/background work |
| `FiberScheduler` | Scheduler-owned fiber workers | Adapting synchronous stackful work |

An async task is not automatically parallel. Two tasks on a cooperative
scheduler can interleave on one thread. Use the thread pool only where parallel
execution and its synchronization costs are desired.

## Drive a cooperative scheduler

```cpp
while (!operation.IsCompleted()) {
    if (!scheduler.RunOne()) {
        WaitForExternalEventOrTimer();
    }
}
```

`RunUntilIdle` drains immediately runnable work. It does not wait for a future
timer to become due. Tests can use `RunOneAt(now)` or `RunUntilIdleAt(now)` to
supply a deterministic monotonic time.

## Yield and delay

```cpp
co_await context.YieldNow();
co_await context.Delay(NGIN::Units::Milliseconds {50.0});
```

Both operations validate the executor and observe cancellation. Scheduling
failure becomes an `AsyncFault`, not a domain error. A non-positive delay can
be ready immediately when the context is valid and not canceled.

## Context propagation

Pass the context supplied by the caller:

```cpp
Task<Response, Error> Handle(TaskContext& context) {
    auto input = co_await Read(context);
    co_return co_await Write(context, input);
}
```

Creating unrelated contexts deep in a call tree breaks cancellation lineage
and makes execution ownership difficult to reason about. Create child contexts
only in an owner/combinator whose policy requires them.

## External drivers

Network and filesystem drivers have their own progress/lifetime contracts. A
driver observes OS completion or runs blocking fallback work; the task context
selects where the awaiting coroutine resumes. Some programs must drive both:

```cpp
while (!operation.IsCompleted()) {
    networkDriver.PollOnce();
    scheduler.RunUntilIdle();
}
```

Follow the subsystem's API reference for whether a driver owns threads or
requires polling.

## Shutdown safely

1. Prevent new submissions.
2. Request cancellation for owned operations.
3. Keep drivers and schedulers running until work terminates.
4. Consume/report root results where policy requires it.
5. Destroy services and buffers used by tasks.
6. Destroy contexts, then their concrete executors/drivers.

Calling `CancelAll` only clears/requests scheduler-owned work according to the
scheduler contract; it does not make arbitrary borrowed application state safe
to destroy immediately.

Next: [async generators](./generators.md) or the
[`TaskContext` reference](../../../reference/cpp/base/async/task-context.md).

