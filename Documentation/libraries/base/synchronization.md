---
title: Learn NGIN.Sync
description: Choose and use mutexes, reader-writer locks, spin locks, semaphores, guards, and atomic conditions correctly.
---

# Learn NGIN.Sync

NGIN.Sync provides small C++ Lockable-compatible wrappers and low-level
coordination primitives. Use them when shared state genuinely crosses execution
contexts and cannot be confined to one thread, scheduler lane, or immutable
snapshot.

Synchronization prevents particular data races; it does not automatically make
an object protocol correct. Start by identifying the shared invariant and who
may access it, then choose the narrowest primitive that protects that invariant.

## Choose a primitive

| Need | Primitive | Cost and constraint |
| --- | --- | --- |
| One owner at a time | `Mutex` | Blocks the thread under contention |
| Same owner must lock recursively | `RecursiveMutex` | Can hide unclear ownership; use deliberately |
| Many readers or one writer | `SharedMutex` / `ReadWriteLock` | Reader/writer fairness is platform-dependent |
| Extremely short, non-blocking critical section | `SpinLock` | Burns/yields CPU while waiting |
| FIFO-style spin acquisition | `TicketLock` | Fairer order, still busy-waits |
| Counted capacity or work permits | `Semaphore<MaxCount>` | Permit count is not object ownership |
| Wait for a generation change | `AtomicCondition` | Predicate/state must be stored separately |
| Scope-bound acquisition | `LockGuard<T>` / `SharedLockGuard<T>` | Preferred way to release on every exit path |

Read [choosing a primitive](./synchronization/choosing-primitive.md) for a
decision process based on wait time, contention, ownership, and fairness.

## Your first protected invariant

```cpp
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/Mutex.hpp>

#include <iostream>

int main() {
    NGIN::Sync::Mutex mutex;
    int counter = 0;

    auto increment = [&] {
        for (int i = 0; i < 10'000; ++i) {
            NGIN::Sync::LockGuard guard {mutex};
            ++counter;
        }
    };

    NGIN::Execution::WorkerThread first {increment};
    NGIN::Execution::WorkerThread second {increment};
    first.Join();
    second.Join();

    std::cout << counter << '\n';
}
```

Expected output:

```text
20000
```

The protected invariant is “every read-modify-write of `counter` occurs while
holding `mutex`.” The guard acquires in its constructor and releases on scope
exit, including early return or exception unwinding.

## Lockable compatibility

Mutex-like types expose both NGIN-style and standard Lockable names:

```cpp
mutex.Lock();       // NGIN style
mutex.Unlock();

mutex.lock();       // BasicLockable compatibility
mutex.unlock();
```

This allows `std::lock_guard`, `std::unique_lock`, and generic Lockable code.
`SharedMutex` and `ReadWriteLock` additionally provide the standard shared-lock
surface, so `std::shared_lock` works.

Prefer a guard to manual lock/unlock. Read [mutexes and guards](./synchronization/mutexes-guards.md)
for exclusive/shared ownership and lock-order rules.

## Semaphores are permits

```cpp
NGIN::Sync::Semaphore<8> slots {8};

void UseLimitedResource() {
    NGIN::Sync::LockGuard permit {slots};
    AccessOneOfEightSlots();
}
```

Acquisition consumes a count; release returns it. The thread releasing a
semaphore need not be the thread that acquired it. This differs from mutex
ownership, even though the Lockable method names allow scope guards.

## AtomicCondition is a generation signal

`AtomicCondition` lets waiters observe a monotonically changing generation:

```cpp
while (!ready.load(std::memory_order_acquire)) {
    const auto observed = condition.Load();
    if (ready.load(std::memory_order_acquire)) {
        break;
    }
    condition.Wait(observed);
}
```

The condition does not store `ready`. The application predicate must be atomic
or protected by a lock and must always be rechecked after waking. Notifications
increment the generation with release semantics, then wake one or all waiters.

Read [semaphores and conditions](./synchronization/semaphores-conditions.md)
for timed waits and correct predicate loops.

## Memory visibility

The locking and condition operations establish their documented acquire/release
ordering. That ordering only helps code participating in the same protocol.
Reading protected data without its lock remains a data race; testing
`HasWaitingThreads()` does not synchronize application state.

## Rules that prevent most bugs

1. Protect an invariant, not isolated lines chosen after a race appears.
2. Keep a global lock order when an operation needs more than one lock.
3. Never suspend a coroutine or perform unbounded I/O while holding a lock.
4. Keep spin-lock critical sections bounded and free of blocking operations.
5. Recheck predicates after every condition wakeup.
6. Make object destruction occur only after all possible users have stopped.
7. Use thread confinement or message passing when shared mutable state is not
   necessary.

Read [correctness and lifetime](./synchronization/correctness-lifetime.md) for
deadlock, destruction, async, and shutdown guidance.

## Common mistakes

| Mistake | Why it fails | Better approach |
| --- | --- | --- |
| Manual `Lock` followed by an early return | Lock is never released | Construct a scope guard immediately |
| Using `SpinLock` around file or network I/O | Waiters consume CPU for an unbounded interval | Use a blocking mutex or redesign ownership |
| Locking A then B in one path and B then A in another | Circular wait can deadlock | Establish and document one lock order |
| Assuming `NotifyOne` stores the predicate | A wake contains no application state | Publish state, then notify; waiter loops on state |
| Destroying a condition with waiters | Waiters still access the object | Stop, notify, join/drain, then destroy |
| Holding a mutex across `co_await` | Resumption may occur later/on another thread and block progress | Snapshot/move state, unlock, then await |

## Continue

### Learn in order

1. [Choosing a primitive](./synchronization/choosing-primitive.md)
2. [Mutexes and guards](./synchronization/mutexes-guards.md)
3. [Semaphores and conditions](./synchronization/semaphores-conditions.md)
4. [Correctness and lifetime](./synchronization/correctness-lifetime.md)

### Look up exact code

- [Synchronization symbol index](../../reference/cpp/base/sync/index.md)
- [Mutex and guard reference](../../reference/cpp/base/sync/mutexes.md)
- [`Semaphore`](../../reference/cpp/base/sync/semaphore.md)
- [`AtomicCondition`](../../reference/cpp/base/sync/atomic-condition.md)
