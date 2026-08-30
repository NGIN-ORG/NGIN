---
title: AtomicCondition
description: Code reference for generation-based waiting, timed waits, notifications, and diagnostics.
---

# `AtomicCondition`

**Header:** `<NGIN/Sync/AtomicCondition.hpp>`  
**Namespace:** `NGIN::Sync`  
**Target:** `NGIN::Base::Foundation`  
**Defined:** [`AtomicCondition.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Sync/AtomicCondition.hpp#L38)

## Declaration summary

```cpp
class AtomicCondition {
public:
    AtomicCondition() noexcept;

    void Wait() noexcept;
    void Wait(UInt32 observedGeneration) noexcept;
    UInt32 Load() const noexcept;

    template<typename TUnit>
    bool WaitFor(const TUnit& duration) noexcept;

    template<typename TUnit>
    bool WaitFor(UInt32 observedGeneration, const TUnit& duration) noexcept;

    void NotifyOne() noexcept;
    void NotifyAll() noexcept;

    UInt32 GetGeneration() const noexcept;
    UInt32 GetWaitingThreadCount() const noexcept;
    bool HasWaitingThreads() const noexcept;
};
```

## Wait contract

`Load` acquire-loads the generation. `Wait(observed)` blocks while that
generation remains observed. Timed waits accept NGIN time quantities; they
return `true` if the generation changed before timeout and `false` on timeout
or non-positive duration.

Always associate the condition with an independently stored predicate and
recheck that predicate in a loop. Generation change is a wake event, not the
application result.

## Notification

`NotifyOne` and `NotifyAll` release-increment the generation before waking
waiters. The 32-bit generation can wrap; protocols compare for change across a
bounded wait rather than treating it as an everlasting unique event ID.

## Diagnostics and lifetime

Waiting-thread counts are transient diagnostics and use relaxed observation.
They cannot prove that destruction or notification omission is safe.
`AtomicCondition` is non-copyable and must outlive all waiters.

