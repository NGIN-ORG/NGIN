---
title: ExecutorRef
description: Code reference for the borrowed type-erased NGIN.Execution executor interface.
---

# `ExecutorRef`

**Header:** `<NGIN/Execution/ExecutorRef.hpp>`  
**Namespace:** `NGIN::Execution`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`ExecutorRef.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/ExecutorRef.hpp#L23)

## Declaration

```cpp
class ExecutorRef final;
```

`ExecutorRef` stores a borrowed state pointer plus callbacks for immediate and
absolute-time submission. It erases the concrete scheduler type without
owning, allocating, or controlling that scheduler.

## Construction

```cpp
constexpr ExecutorRef() noexcept;

template<typename TScheduler>
static constexpr ExecutorRef From(TScheduler& scheduler) noexcept;

[[nodiscard]] constexpr bool IsValid() const noexcept;
```

`From` requires `TScheduler::Execute(WorkItem)` and
`TScheduler::ExecuteAt(WorkItem, TimePoint)`. A default reference is invalid.
The referenced scheduler must outlive the reference and all code/contexts that
can submit through it.

## Immediate submission

```cpp
[[nodiscard]] ScheduleResult Execute(WorkItem item) const noexcept;

template<typename F>
[[nodiscard]] ScheduleResult Execute(F&& job) const;

[[nodiscard]] ScheduleResult Execute(std::coroutine_handle<> coroutine) const noexcept;
[[nodiscard]] ScheduleResult Execute(Utilities::Callable<void()> job) const;
```

An invalid reference returns `InvalidExecutor`. Empty type-erased callables are
rejected. Callable storage allocation failure returns `ResourceExhausted`.

## Delayed submission

```cpp
[[nodiscard]] ScheduleResult ExecuteAt(
    WorkItem item,
    Time::TimePoint resumeAt) const noexcept;

template<typename TUnit>
[[nodiscard]] ScheduleResult ExecuteAfter(
    WorkItem item,
    const TUnit& delay) const noexcept;
```

Callable and coroutine overloads exist for both forms. `TUnit` must be a time
quantity. Non-positive relative delay submits immediately. Positive values are
converted to nanoseconds, rounded upward, added to `MonotonicClock::Now()`, and
saturated on overflow.

## Ownership and thread safety

Successful submission consumes the item into the concrete scheduler. Failure
occurs before acceptance. Thread safety and callback execution semantics are
those of the referenced scheduler; type erasure adds no synchronization.

## Related

- [`WorkItem`](./work-item.md)
- [Scheduler classes](./schedulers.md)
- [Submitting work guide](../../../../libraries/base/execution/submitting-work.md)

