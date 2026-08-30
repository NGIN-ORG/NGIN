---
title: NGIN.Base Async API
description: Task, Operation, Completion, cancellation, context, generators, and async combinators.
---

# NGIN.Base Async API

**Include:** `<NGIN/Async.hpp>`  
**Target:** `NGIN::Base::Execution`  
**Namespace:** `NGIN::Async`

The async layer provides cold, move-only coroutine tasks. It does not create a
thread or scheduler. A `TaskContext` binds work to an executor and carries its
cancellation token.

## Core types

| Type | Purpose | Ownership rule |
| --- | --- | --- |
| `Task<T, E = NoError>` | Cold coroutine producing `T` or terminal failure | Move-only; creation does not start it |
| `Operation<T, E = NoError>` | Handle to started root work | Owns the root result; result can be taken once |
| `Completion<T, E>` | Value-owned terminal result | Distinguishes success, domain error, cancellation, and fault |
| `TaskContext` | Executor and cancellation context | Must outlive work using it |
| `CancellationSource` | Requests cooperative cancellation | Tokens and registrations share its state |
| `CancellationToken` | Observes and registers for cancellation | Does not itself request cancellation |
| `AsyncGenerator<T, E>` | Asynchronous multi-value sequence | Advance one item at a time with `Next` |
| `AsyncFault` | Infrastructure/runtime failure | Do not use for ordinary domain errors |

## Start root work

```cpp
Operation<T, E> Spawn(TaskContext& context, Task<T, E>&& task);
void Detach(TaskContext& context, Task<T, E>&& task);
Completion<T, E> SyncWait(TaskContext& context, Task<T, E>&& task);
```

- Use `Spawn` when a boundary needs to poll completion and consume a result.
- Use `Detach` only when no caller owns the result. The coroutine frame stays
  alive until execution and retained work release it.
- Use `SyncWait` at synchronous integration boundaries. Do not block a normal
  coroutine path with it.

```cpp
NGIN::Execution::CooperativeScheduler scheduler;
NGIN::Async::TaskContext context {scheduler};

auto operation = NGIN::Async::Spawn(context, LoadValue(context));
scheduler.RunUntilIdle();

auto completion = operation.TakeResult();
if (!completion) {
    return HandleFailure(completion);
}
Use(completion.Value());
```

Calling `TakeResult()` before completion, or more than once, is programmer
error. Use `IsCompleted()` before taking a result when the scheduler is not run
to idle.

## Write and compose tasks

```cpp
enum class LoadError { NotFound, InvalidData };

NGIN::Async::Task<int, LoadError>
LoadValue(NGIN::Async::TaskContext& context) {
    co_await context.YieldNow();
    co_return 42;
}

NGIN::Async::Task<int, LoadError>
LoadTwice(NGIN::Async::TaskContext& context) {
    int first = co_await LoadValue(context);
    int second = co_await LoadValue(context);
    co_return first + second;
}
```

A child success yields its value. A child domain error, cancellation, or fault
propagates through the awaiting parent automatically.

For explicit non-success, return a completion:

```cpp
co_return NGIN::Async::Completion<int, LoadError>::DomainFailure(
    LoadError::InvalidData);
```

`Expected<T, E>`, `Unexpected<E>`, and bare `E` values are also accepted where
the task promise can identify them. In a `Task<void, E>`, use the explicit
awaiters `DomainFailure(error)`, `Canceled()`, or `Faulted(fault)`.

## Inspect completion

```cpp
if (result.Succeeded()) {
    Use(result.Value());
} else if (result.IsDomainError()) {
    Handle(result.DomainError());
} else if (result.IsCanceled()) {
    HandleCancellation();
} else {
    HandleInfrastructureFault(result.Fault());
}
```

Do not collapse these states into one generic error. Domain errors are part of
your API; cancellation is requested control flow; faults describe scheduler,
registration, invalid-use, or captured-exception failures.

## TaskContext operations

| Operation | Result |
| --- | --- |
| `GetExecutor()` | The bound `Execution::ExecutorRef` |
| `GetCancellationToken()` | The context's token |
| `CheckCancellation()` | Whether cancellation is already requested |
| `YieldNow()` | A cancellation-aware reschedule point |
| `Delay(duration)` | A cancellation-aware timed suspension |

A non-positive delay can complete without a timer, but still requires a valid
executor and observes cancellation. Scheduling and cancellation-registration
failures become `AsyncFault` terminal states.

## Cancellation

Cancellation is cooperative. Requesting it does not destroy running work.

```cpp
NGIN::Async::Task<void, LoadError>
ReadLoop(NGIN::Async::TaskContext& context) {
    while (!context.CheckCancellation()) {
        co_await ReadOne(context);
    }
    co_await NGIN::Async::Canceled();
}
```

`CancellationSource` may use a caller-provided
`std::pmr::memory_resource`. That resource must outlive the source, copied
tokens, and registrations. Registration exhaustion reports
`CancellationRegistrationError::ResourceExhausted`.

## Combinators

```cpp
auto values = co_await NGIN::Async::WhenAll(
    context, ReadA(context), ReadB(context));

auto winner = co_await NGIN::Async::WhenAny(
    context,
    [](auto& child) { return ReadA(child); },
    [](auto& child) { return ReadB(child); });
```

`WhenAll` consumes its tasks. `WhenAny` accepts factories, creates linked child
contexts, records the first terminal child, requests cancellation for losers,
and drains them before returning. A loser that ignores cancellation adds to
completion latency.

## Async generators

```cpp
auto next = co_await generator.Next(context);
```

`Next(context)` returns `Task<GeneratorNext<T>, E>`. Do not call `Next`
concurrently on the same generator.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| Task body never runs | The task is cold | Await it or pass it to `Spawn`, `Detach`, or `SyncWait` |
| `InvalidTaskUsage` fault | Missing/invalid executor or invalid lifecycle use | Keep the scheduler alive and pass its context |
| Parent never finishes after `WhenAny` | A losing child does not finish | Add cancellation-aware suspension/check points |
| Use-after-free around async work | Captured data or context expired | Move ownership into the coroutine or extend the owner's lifetime |

**Source:** [`NGIN/Async`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async)

