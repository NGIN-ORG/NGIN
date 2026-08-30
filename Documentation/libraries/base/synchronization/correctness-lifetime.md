---
title: Synchronization correctness and lifetime
description: Prevent deadlocks, lock misuse, coroutine suspension hazards, and destruction races.
---

# Synchronization correctness and lifetime

## A lock protects a protocol

List the fields and operations covered by each lock. Every access—including
diagnostics and “harmless” reads—must follow the same protocol unless the field
is independently atomic.

Locking one method while another returns an unprotected mutable reference does
not make the type thread-safe. Prefer operations that complete under the lock
or return an owned snapshot.

## Never suspend while locked

Do not hold a mutex/shared guard across `co_await`, fiber yield, blocking I/O,
or an unbounded callback. Suspension lengthens the critical section and may
resume on another execution context. NGIN guards model lexical ownership, not
cross-suspension asynchronous locks.

Use this shape:

```cpp
State snapshot;
{
    NGIN::Sync::LockGuard guard {mutex};
    snapshot = CopyNeededState();
}

co_await Send(snapshot);
```

After the await, reacquire and validate a version if you must apply a result to
state that may have changed.

## Shutdown

A synchronization object must outlive all threads and callbacks that might
lock or wait on it. The owner should:

1. publish a stop predicate;
2. notify all waiters;
3. join threads or drain scheduler work;
4. verify no user remains;
5. destroy protected state and synchronization primitives.

Destroying a mutex while locked or an atomic condition while waiters remain is
undefined/invalid lifetime use. `HasWaitingThreads()` is not a synchronization
barrier and cannot replace joining.

## Diagnose deadlock

Capture each thread’s current lock/wait point, then look for a cycle in
ownership. Logging while holding the affected locks can itself deadlock; emit
minimal diagnostics through an independent path or record lock-order events in
a bounded debug buffer.

