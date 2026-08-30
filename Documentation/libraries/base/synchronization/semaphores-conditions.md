---
title: Semaphores and atomic conditions
description: Coordinate counted permits and generation-based wakeups without losing state or notifications.
---

# Semaphores and atomic conditions

## Counted permits

`Semaphore<MaxCount>` wraps a counting semaphore. Its constructor sets the
initial count; `Lock`/`lock` acquires one permit, `TryLock` attempts without
blocking, and `Unlock` releases one.

```cpp
NGIN::Sync::Semaphore<4> permits {4};

if (permits.TryLock()) {
    StartOneOperation();
    permits.Unlock();
}
```

Keep the count between zero and `MaxCount`. Releasing more permits than the
protocol acquired is a logic error. A semaphore limits concurrency; it does not
protect arbitrary shared container operations performed inside the work.

## Generation-based notification

`AtomicCondition` stores a 32-bit generation. `NotifyOne`/`NotifyAll` increment
it and wake waiters. A correct wait captures a generation and loops on an
external predicate:

```cpp
while (!stopping.load(std::memory_order_acquire) && queue.Empty()) {
    const auto generation = wake.Load();

    // Recheck after observing the generation so a producer cannot publish
    // state and notify between the first check and the wait.
    if (!queue.Empty()) {
        break;
    }

    wake.Wait(generation);
}
```

The concrete queue must have its own safe concurrent/locked operations. The
condition only provides the wake generation.

## Timed wait

```cpp
const auto generation = wake.Load();
const bool changed = wake.WaitFor(
    generation,
    NGIN::Units::Milliseconds {100.0});
```

`true` means the generation changed before the timeout; it does not mean the
application predicate is true. Recheck it. Non-positive durations return
without waiting.

`GetWaitingThreadCount` and `HasWaitingThreads` are diagnostics. They can change
immediately and must never be used as proof that notifying or destroying is
safe.

