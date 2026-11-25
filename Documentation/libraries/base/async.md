---
title: Learn NGIN.Async
description: Start with your first coroutine task, then learn execution, results, cancellation, composition, and lifetime one concept at a time.
---

# Learn NGIN.Async

NGIN.Async is the coroutine layer in NGIN.Base. It lets you write asynchronous
operations that return values, report typed errors, cooperate with
cancellation, and resume through an executor you own.

If you have never used it, start here. You do not need to understand coroutine
promise types or frame internals to complete the examples.

## What you can build

With NGIN.Async you can:

- start work on an inline, caller-driven, thread-pool, or fiber scheduler;
- write `co_await` control flow without using exceptions for expected errors;
- distinguish a normal domain error from cancellation and an infrastructure
  fault;
- cancel related work through explicit tokens and contexts;
- wait for several tasks with `WhenAll` or race them with `Race`;
- expose asynchronous sequences with `AsyncGenerator<T>`;
- bridge a synchronous program boundary with `SyncWait`.

NGIN.Async does **not** install a global event loop, create hidden worker
threads, or start a task merely because you called its function.

## The four objects to understand first

| Object | What it means | Who owns it |
| --- | --- | --- |
| `Task<T, E>` | A cold recipe for asynchronous work | The caller until it is awaited or started |
| `TaskContext` | The executor and cancellation state used by that work | Your runtime/application scope |
| `Operation<T, E>` | A handle to a root task that has been started | The handle owns result access, and the spawning scope owns execution |
| `Completion<T, E>` | The final success, domain error, cancellation, or fault | The code that consumes the result |

Await a child directly for sequential work. Use `WhenAll` for a fixed group and
`WithScope(body)` for dynamic children. An ordinary await resumes on the caller's
executor. A scope closes admission, requests stop, and joins unfinished children
when the body exits.

## Your first task

This complete program creates a task, starts it, drives its scheduler, and
reads the value.

```cpp
#include <NGIN/Async/SyncWait.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>

#include <iostream>

enum class DemoError {
    InvalidInput,
};

NGIN::Async::Task<int, DemoError>
Compute(NGIN::Async::TaskContext& context) {
    co_await context.YieldNow();
    co_return 7;
}

int main() {
    NGIN::Execution::ThreadPoolScheduler scheduler(1);
    NGIN::Async::TaskContext context {scheduler};

    auto result = NGIN::Async::SyncWait(context, Compute(context));
    if (!result) {
        std::cerr << "Compute did not succeed\n";
        return 1;
    }

    std::cout << result.Value() << '\n';
    return 0;
}
```

Expected output:

```text
7
```

### What happened

1. Calling `Compute(context)` created a cold `Task`. Its body had not run.
2. `SyncWait` started the task on the worker and waited for its retirement.
3. `YieldNow` queued a continuation. The worker resumed it and the task returned 7.

## Cold tasks and root boundaries

A synchronous entry point can use `SyncWait` with an independently driven executor.
An event loop can own a `TaskSupervisor`, check the result of `Spawn`, and keep
pumping its executor through join. Use `Transfer` when the supervisor should
own background work and its diagnostics through shutdown.

## Await child work

Inside a task, composition is direct:

```cpp
NGIN::Async::Task<int, DemoError>
LoadPart(NGIN::Async::TaskContext& context) {
    co_await context.YieldNow();
    co_return 3;
}

NGIN::Async::Task<int, DemoError>
LoadTotal(NGIN::Async::TaskContext& context) {
    int first = co_await LoadPart(context);
    int second = co_await LoadPart(context);
    co_return first + second;
}
```

The parent suspends while the child runs. A successful child produces `T`. A
child domain error, cancellation, or fault propagates to the parent
automatically; code after the failed `co_await` does not run.

## Success and three kinds of non-success

`Completion<T, E>` keeps failures separate:

| State | Meaning | Example |
| --- | --- | --- |
| Success | The operation produced `T` | Parsed value, received bytes |
| Domain error | An expected problem described by your `E` | Not found, invalid input, disconnected |
| Canceled | A caller requested cooperative cancellation | User closed a screen, timeout policy canceled work |
| Fault | Runtime/infrastructure failure | Invalid executor, scheduling failure, captured exception |

Handle all four at a root boundary:

```cpp
auto result = operation.TakeResult();
if (result.Succeeded()) {
    Use(result.Value());
} else if (result.IsDomainError()) {
    HandleDomainError(result.DomainError());
} else if (result.IsCanceled()) {
    HandleCancellation();
} else {
    HandleFault(result.Fault());
}
```

Return a domain failure from a value task:

```cpp
NGIN::Async::Task<int, DemoError>
ParseCount(NGIN::Async::TaskContext&, bool valid) {
    if (!valid) {
        co_return NGIN::Async::Completion<int, DemoError>::DomainFailure(
            DemoError::InvalidInput);
    }
    co_return 42;
}
```

`Expected<T, E>`, `Unexpected<E>`, and a bare `E` can also be returned where
the promise can identify them. For `Task<void, E>`, await an explicit terminal
awaiter:

```cpp
if (!valid) {
    co_await NGIN::Async::DomainFailure(DemoError::InvalidInput);
    co_return;
}
```

Read [errors and completions](./async/errors.md) for the complete decision
model and result-consumption rules.

## Cancellation

Cancellation asks work to stop; it does not destroy the coroutine or interrupt
arbitrary code.

```cpp
NGIN::Async::Task<void, DemoError>
ProcessItems(NGIN::Async::TaskContext& context) {
    while (HasMoreItems()) {
        if (context.CheckCancellation()) {
            co_await NGIN::Async::Canceled();
            co_return;
        }

        ProcessOneItem();
        co_await context.YieldNow();
    }
}
```

`YieldNow()` and `Delay(...)` observe the context token automatically. CPU-only
loops need explicit checks. Child contexts used by structured combinators link
to their parent cancellation.

Read [cancellation](./async/cancellation.md) before adding timeouts,
registrations, or external cancellation sources.

## Run work together

Use `WhenAll` when every result is needed:

```cpp
auto results = co_await NGIN::Async::WhenAll(
    context,
    LoadUser(context),
    LoadSettings(context));
```

`WhenAll` consumes its tasks. Pass fresh tasks or move existing ones.

Use `Race` when the first terminal child decides the result:

```cpp
auto winner = co_await NGIN::Async::Race(
    context,
    [](NGIN::Async::TaskContext& child) {
        return ReadPrimary(child);
    },
    [](NGIN::Async::TaskContext& child) {
        return ReadReplica(child);
    });
```

Factories let `Race` give each child a linked context. After one child
finishes, it requests cancellation for the losers and drains them before
returning. A losing task that ignores cancellation delays the result.

Read [combining tasks](./async/composition.md) for return shapes, failure
propagation, cancellation, and lifetime rules.

## Delay and scheduling

```cpp
co_await context.Delay(NGIN::Units::Milliseconds {250.0});
co_await context.YieldNow();
```

These operations schedule continuation through the context executor. A
`CooperativeScheduler` needs its owner to keep driving it. A
`ThreadPoolScheduler` owns workers. An `InlineScheduler` resumes immediately
and can introduce re-entrancy. Choose the execution model separately from the
task logic.

Read [contexts and schedulers](./async/runtime.md) for setup patterns and
shutdown order.

## Async sequences

Use `AsyncGenerator<T, E>` when one operation produces several values over
time:

```cpp
auto next = co_await generator.Next(context);
```

Each `Next` returns a task whose success says either “one value is available”
or “the sequence is complete.” Do not call `Next` concurrently on the same
generator. See [async generators](./async/generators.md).

## Lifetimes you must preserve

- `TaskContext` and its concrete scheduler must outlive work that uses them.
- Captured references must remain valid until the task terminates.
- Buffers passed to an async I/O operation must satisfy that operation's
  lifetime contract.
- A task is move-only and single-consumer. Do not await it twice.
- An operation result can be taken once. Prefer `TryTakeResult()` when polling
  code cannot prove completion.
- Cancel before tearing down dependencies, then drive/drain work until it
  reaches a terminal state.

Dropping a running `Operation` releases the result owner; it does not perform
unsafe immediate frame destruction. That memory-safety guarantee does not make
unowned failures observable.

## Common beginner problems

| What you see | Why | What to do |
| --- | --- | --- |
| The coroutine body never runs | The task was created but not awaited/spawned | Await it inside a task or use `Spawn` at the root |
| It stops after `YieldNow` | A cooperative scheduler is not being driven | Call `RunOne`/`RunUntilIdle` from the owning loop |
| `TakeResult` fails/asserts | Work is not complete or the result was already taken | Check `IsCompleted` or use `TryTakeResult`, consume once |
| Parent code after `co_await` never runs | Child propagated domain error, cancellation, or fault | Handle it at the intended boundary |
| `Race` takes too long | A losing task does not cooperate with cancellation | Add cancellation-aware waits/checks |
| Data is invalid after resumption | The task captured a shorter-lived reference | Move ownership into the task or extend the owner's lifetime |
| `InvalidTaskUsage` fault | Context executor is missing/invalid or lifecycle use is invalid | Keep a valid scheduler/context alive |

## Where to go next

### Learn in order

1. [Your first async operation](./async/first-task.md)
2. [Errors and completions](./async/errors.md)
3. [Cancellation](./async/cancellation.md)
4. [Combining tasks](./async/composition.md)
5. [Contexts and schedulers](./async/runtime.md)
6. [Async generators](./async/generators.md)

### Look up exact code

- [Async symbol index](/reference/doxygen/base/namespaceNGIN_1_1Async.html)
- [`Task<T, E>` reference](/reference/doxygen/base/classNGIN_1_1Async_1_1Task.html)
- [`Completion<T, E>` reference](/reference/doxygen/base/classNGIN_1_1Async_1_1Completion.html)
- [`TaskContext` reference](/reference/doxygen/base/classNGIN_1_1Async_1_1TaskContext.html)
- [Cancellation reference](/reference/doxygen/base/namespaceNGIN_1_1Async.html)
- [Combinator reference](/reference/doxygen/base/namespaceNGIN_1_1Async.html)
