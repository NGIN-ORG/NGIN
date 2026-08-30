---
title: TaskContext
description: Code reference for executor binding, cancellation binding, yielding, and delays in NGIN.Async.
---

# `TaskContext`

**Header:** `<NGIN/Async/TaskContext.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`TaskContext.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/TaskContext.hpp#L15)

## Construction

```cpp
explicit TaskContext(
    NGIN::Execution::ExecutorRef executor,
    CancellationToken cancellation = {}) noexcept;

template<typename TScheduler>
explicit TaskContext(
    TScheduler& scheduler,
    CancellationToken cancellation = {}) noexcept;
```

The scheduler constructor creates a non-owning `ExecutorRef`. The scheduler
must satisfy the executor operations and outlive all uses of the context.

## Executor binding

```cpp
[[nodiscard]] bool HasExecutor() const noexcept;
void BindExecutor(NGIN::Execution::ExecutorRef executor) noexcept;

template<typename TScheduler>
void BindExecutor(TScheduler& scheduler) noexcept;

[[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() const noexcept;
```

Rebinding changes subsequent use of this context. It does not migrate already
submitted work or extend any scheduler lifetime.

## Cancellation binding

```cpp
void BindCancellationToken(CancellationToken token) noexcept;
[[nodiscard]] TaskContext
    WithCancellationToken(CancellationToken token) const noexcept;

void BindLinkedCancellationToken(CancellationToken token) noexcept;
[[nodiscard]] TaskContext
    WithLinkedCancellationToken(CancellationToken token) const noexcept;

[[nodiscard]] CancellationToken GetCancellationToken() const noexcept;
[[nodiscard]] bool IsCancellationRequested() const noexcept;
[[nodiscard]] bool CheckCancellation() const noexcept;
```

Replacement binding discards this context's prior linked-token ownership.
Linked binding creates cancellation state that observes both the existing and
supplied token. Linked state can allocate through `std::shared_ptr`.

## Awaitable operations

```cpp
[[nodiscard]] auto YieldNow() const noexcept;

template<typename TUnit>
    requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
[[nodiscard]] auto Delay(const TUnit& duration) const noexcept;
```

`YieldNow` always attempts rescheduling through the executor. `Delay` computes
an absolute monotonic deadline. Both observe cancellation and translate
invalid executor, schedule rejection, or cancellation-registration failure
into terminal task state.

## Thread safety and lifetime

Treat a context as configuration owned by its task tree. Do not concurrently
rebind it while tasks read it. Copies share/borrow cancellation and executor
state according to those members; they do not own the concrete executor.

## Related symbols

- [Cancellation types](./cancellation.md)
- [Execution scheduler reference](../execution.md)
- [Contexts and schedulers guide](../../../../libraries/base/async/runtime.md)
