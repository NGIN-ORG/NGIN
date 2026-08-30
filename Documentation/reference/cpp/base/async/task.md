---
title: Task<T, E>
description: Code reference for the cold, move-only NGIN.Async coroutine task type.
---

# `Task<T, E>`

**Header:** `<NGIN/Async/Task.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`Task.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/Task.hpp#L529)

## Declaration

```cpp
template<typename T, typename E = NoError>
class Task : public BaseTask;

template<typename E>
class Task<void, E> : public BaseTask;
```

A `Task` uniquely owns a cold coroutine frame. It begins when it is awaited,
passed to `Spawn`/`Detach`/`SyncWait`, or otherwise started through the
documented task machinery.

## Template parameters

| Parameter | Meaning | Requirements |
| --- | --- | --- |
| `T` | Successful result value | Must satisfy the operations used by the promise/consumer; moved out on successful await |
| `E` | Recoverable domain error | Defaults to `NoError`; must support the selected return/propagation operations |

Use `Task<void, E>` when success has no value.

## Construction and ownership

```cpp
Task() noexcept;
explicit Task(handle_type handle) noexcept;
Task(Task&& other) noexcept;
Task& operator=(Task&& other) noexcept;
Task(const Task&) = delete;
Task& operator=(const Task&) = delete;
~Task();
```

The public default constructor creates an empty task. The handle constructor is
used by coroutine machinery. Moving transfers the frame and started state.
Destroying an unstarted task releases its frame. Destroying a running task
releases owner interest; retained execution/continuation references keep the
frame alive until safe destruction.

## State inspection

```cpp
[[nodiscard]] bool IsStarted() const noexcept;
[[nodiscard]] bool IsCompleted() const noexcept;
[[nodiscard]] bool IsFaulted() const noexcept;
[[nodiscard]] bool IsCanceled() const noexcept;
```

| Member | Returns |
| --- | --- |
| `IsStarted` | Whether start was claimed for this task |
| `IsCompleted` | Whether a terminal completion was published |
| `IsFaulted` | Whether the published completion is a fault |
| `IsCanceled` | Whether the published completion is canceled |

When exception capture is enabled at build time, `GetException()` returns the
captured unhandled exception pointer, if any.

## Awaiting

```cpp
[[nodiscard]] PropagationAwaiter operator co_await() & noexcept;
[[nodiscard]] OwnedPropagationAwaiter operator co_await() && noexcept;
[[nodiscard]] CancellablePropagationAwaiter
    WithCancellation(TaskContext& context) noexcept;
```

- Awaiting an lvalue borrows the task; the task object must remain alive.
- Awaiting an rvalue transfers task ownership into the awaiter.
- `WithCancellation` observes the supplied context while awaiting.
- Success resumes with `T` (`void` for the specialization).
- Domain error, cancellation, and fault propagate into a compatible parent
  task promise instead of returning a `Completion` to the await expression.

A task is single-consumer. Installing more than one continuation is invalid
task use.

## Delay helper

```cpp
template<typename TUnit>
    requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
static Task<void, E> Delay(TaskContext& context, const TUnit& duration);
```

This task awaits `context.Delay(duration)` and completes without a value. The
context executor and cancellation contract apply.

## Coroutine return behavior

For `Task<T, E>`, `co_return` accepts a successful `T` and the supported
explicit/expected-like domain result forms implemented by the promise. The
unambiguous explicit forms are:

```cpp
co_return value;
co_return Completion<T, E>::DomainFailure(error);
co_return Completion<T, E>::Canceled();
co_return Completion<T, E>::Faulted(fault);
```

For `Task<void, E>`, use `co_return;` for success and the terminal awaiters
`DomainFailure(error)`, `Canceled()`, and `Faulted(fault)` for non-success.

## Preconditions and invalid use

- Do not copy a task.
- Do not await/start the same task more than once.
- Do not use a moved-from task as work.
- Keep borrowed captures and any referenced `TaskContext` alive until terminal
  completion.
- A parent promise must support NGIN.Async propagation.

Invalid lifecycle/continuation state becomes an `AsyncFault` or triggers the
documented debug assertion where the operation is a programmer precondition.

## Related symbols

- [`Operation<T, E>`](./operation.md)
- [`Completion<T, E>`](./completion.md)
- [`TaskContext`](./task-context.md)
- [Learn tasks from the beginning](../../../../libraries/base/async.md)

