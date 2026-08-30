---
title: Operation<T, E> and root task functions
description: Code reference for started root operations, Spawn, Detach, SyncWait, and one-time result consumption.
---

# `Operation<T, E>` and root task functions

**Header:** `<NGIN/Async/Task.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`Task.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/Task.hpp#L1613)

## Declaration

```cpp
template<typename T, typename E = NoError>
class Operation;

template<typename E>
class Operation<void, E>;
```

An operation owns a started root task's result access. It is move-only and
single-consumer.

## State and result members

```cpp
[[nodiscard]] bool IsValid() const noexcept;
[[nodiscard]] bool IsCompleted() const noexcept;
[[nodiscard]] bool IsFaulted() const noexcept;
[[nodiscard]] bool IsCanceled() const noexcept;

[[nodiscard]] std::optional<Completion<T, E>> TryTakeResult();
[[nodiscard]] Completion<T, E> TakeResult();
```

`TryTakeResult` returns empty while incomplete and after prior consumption.
`TakeResult` calls that operation and returns an
`AsyncFaultCode::InvalidTaskUsage` fault if the result is unavailable. Neither
blocks.

## Await an operation

```cpp
[[nodiscard]] Awaiter operator co_await() & noexcept;
[[nodiscard]] OwnedAwaiter operator co_await() && noexcept;
```

Awaiting an operation returns its complete `Completion<T, E>` rather than
propagating the terminal state as a child `Task` does. The lvalue form borrows
the operation; the rvalue form owns it through suspension.

## `Spawn`

```cpp
template<typename T, typename E>
[[nodiscard]] Operation<T, E>
Spawn(TaskContext& context, Task<T, E>&& task) noexcept;
```

Transfers the task frame, binds the context/executor, and submits it. An empty
task produces an invalid operation. An invalid executor or rejected submission
produces a terminal fault in a valid operation.

## `Detach`

```cpp
template<typename T, typename E>
void Detach(TaskContext& context, Task<T, E>&& task) noexcept;
```

Starts through `Spawn`, then releases result ownership. The running frame
remains retained until completion. No caller can consume the terminal result.

## `SyncWait`

```cpp
template<typename T, typename E>
[[nodiscard]] Completion<T, E>
SyncWait(TaskContext& context, Task<T, E>&& task);
```

Starts, blocks until terminal completion, and consumes the result. Do not call
it from a thread needed to make the task's executor or external driver progress.

## Lifetime

Destroying an incomplete operation detaches owner interest; it does not
destroy a frame still retained by execution or continuations. This prevents a
frame lifetime race but also means dropping the operation discards result
observation.

## Related symbols

- [`Task<T, E>`](./task.md)
- [`Completion<T, E>`](./completion.md)
- [`TaskContext`](./task-context.md)

