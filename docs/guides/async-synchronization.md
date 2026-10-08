# Synchronizing coroutine tasks

`NGIN::Async::AsyncSemaphore` provides counting permits for async work in
NGIN.Base's Execution component. Pending acquisition suspends the coroutine;
`NGIN::Sync::Semaphore::Lock()` instead blocks the calling thread.

```cpp
#include <NGIN/Async/AsyncSemaphore.hpp>
#include <NGIN/Async/ExecutorTransitions.hpp>

NGIN::Async::Task<void> Update(NGIN::Async::AsyncSemaphore& semaphore)
{
    auto permit = co_await semaphore.AcquireAsync();
    // With AsyncSemaphore(1, 1), this section has exclusive access.
    co_await NGIN::Async::YieldNow();
    // The guard returns its permit when this scope exits.
}
```

Construct `AsyncSemaphore(initialCount, maximumCount)` with a positive maximum
and an initial count no larger than that maximum. Use `(1, 1)` for mutual
exclusion, or `(n, n)` to limit concurrent work to `n` tasks. Keep the returned
guard alive across the work it protects. Guards are move-only and may release
early through `Permit::Release()`.

For manual permit ownership, await `WaitAsync()` and balance each success with
`Release()`. `TryAcquire()` attempts manual acquisition without waiting.
`CurrentCount()` is a snapshot of available permits. Releasing above the maximum
is a programmer error.

Inside `Task<T, E>` with a domain-error type, use `WaitAsync<E>()` or
`AcquireAsync<E>()` to match that type. Semaphore failures remain infrastructure
faults; cancellation preserves its stop reason.

Waits inherit the caller's executor, cancellation token, and allocation resource.
Queued waiters receive permits in FIFO order and resume through their executor.
Cancellation removes a pending waiter and propagates its stop reason. If release
wins the race, acquisition succeeds even if cancellation arrives before delivery.
Admission/allocation failures propagate an AsyncFault without consuming a permit.
There is no implicit deadline timer.

The semaphore must outlive all returned tasks, calls, and guards. Cancel and join
outstanding work before destruction; destruction does not cancel waiters. Keep
the executor and allocation resource alive through pending delivery.

See the [complete runnable example](../../Dependencies/NGIN/NGIN.Base/examples/AsyncSemaphore/main.cpp)
and the [NGIN.Base async contract](../../Dependencies/NGIN/NGIN.Base/docs/Async.md).
