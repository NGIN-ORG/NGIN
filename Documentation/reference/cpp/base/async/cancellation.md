---
title: Cancellation API
description: Code reference for CancellationSource, CancellationToken, CancellationRegistration, callbacks, and timeouts.
---

# Cancellation API

**Header:** `<NGIN/Async/Cancellation.hpp>`  
**Namespace:** `NGIN::Async`  
**Target:** `NGIN::Base::Execution`

## `CancellationSource`

**Defined:** [`Cancellation.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Async/Cancellation.hpp#L280)

```cpp
class CancellationSource {
public:
    explicit CancellationSource(
        std::pmr::memory_resource* resource =
            std::pmr::get_default_resource());

    void Cancel() noexcept;
    [[nodiscard]] CancellationToken GetToken() const noexcept;
    [[nodiscard]] bool IsCancellationRequested() const noexcept;

    [[nodiscard]] NGIN::Execution::ScheduleResult CancelAt(
        NGIN::Execution::ExecutorRef executor,
        NGIN::Time::TimePoint at) noexcept;

    template<typename TUnit>
    [[nodiscard]] NGIN::Execution::ScheduleResult CancelAfter(
        NGIN::Execution::ExecutorRef executor,
        const TUnit& delay) noexcept;
};
```

`Cancel` is idempotent and fires active registrations once. Scheduled
cancellation returns submission status; an already-canceled source is a
successful no-op.

The memory resource must outlive the source, every copied token, and every
registration created from its state.

## `CancellationToken`

```cpp
class CancellationToken {
public:
    [[nodiscard]] bool HasState() const noexcept;
    [[nodiscard]] bool IsCancellationRequested() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] CancellationRegistrationResult Register(
        CancellationRegistration& registration,
        NGIN::Execution::ExecutorRef executor,
        std::coroutine_handle<> continuation,
        bool (*callback)(void*) noexcept,
        void* callbackState) const noexcept;
};
```

An empty token has no state and is never requested. Registration arguments
describe where/how the cancellation action runs; use the focused overload
shape from the checked-out header when implementing a callback.

## `CancellationRegistration`

The registration is move-only RAII state. `Reset()` unregisters it. `IsValid`
reports whether it currently owns a registration. Destroy/reset it before any
callback state it borrows.

## Registration result

```cpp
enum class CancellationRegistrationError : std::uint8_t {
    InvalidTarget,
    ResourceExhausted,
};

using CancellationRegistrationResult =
    std::expected<void, CancellationRegistrationError>;
```

`InvalidTarget` means neither a callback nor resumable coroutine was supplied.

## Concurrency

Cancellation can race completion and registration reset. Callback logic must
be safe to observe a request concurrently and must not assume the operation is
otherwise unfinished. The library serializes its registration state; it does
not synchronize arbitrary callback-owned data.

## Related guides

- [Cancellation tutorial](../../../../libraries/base/async/cancellation.md)
- [`TaskContext`](./task-context.md)
