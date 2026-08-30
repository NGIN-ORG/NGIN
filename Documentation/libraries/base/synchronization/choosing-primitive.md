---
title: Choosing a synchronization primitive
description: Decide between confinement, mutexes, reader-writer locks, spin locks, semaphores, and generation conditions.
---

# Choosing a synchronization primitive

Before selecting a lock, ask whether state must be shared. Immutable snapshots,
single-thread ownership, scheduler lanes, and message passing often produce a
simpler protocol than shared mutation.

## Decision sequence

1. **Can one execution context own the state?** Confine it and submit messages.
2. **Is this a count rather than ownership?** Use a semaphore.
3. **Are there long read phases with rare writes?** Consider a shared mutex.
4. **Can every critical section finish in a few instructions without blocking?**
   A spin/ticket lock may be justified after measurement.
5. **Otherwise**, use `Mutex` and a scope guard.

## Mutex versus shared mutex

Use `Mutex` by default. A shared mutex helps only when reads are frequent,
concurrent, and sufficiently long to offset extra coordination cost. A read
that updates a cache, lazy field, metric, or reference count may still be a
write under your invariant.

`ReadWriteLock` is a naming adapter over shared/exclusive behavior. Choose it
when `StartRead`/`StartWrite` communicates the domain more clearly; choose
`SharedMutex` for standard Lockable vocabulary.

## Spin lock versus blocking lock

`SpinLock` and `TicketLock` repeatedly poll/yield. They are appropriate only
when the holder cannot sleep, allocate unpredictably, call foreign code, or
wait for another resource. Oversubscription and preemption can make them far
worse than a blocking mutex.

`TicketLock` gives FIFO-style ticket order, reducing starvation at the cost of
all waiters observing the shared serving counter. Fair order does not make a
long critical section cheap.

## Recursive mutex

Choose `RecursiveMutex` only when re-entry by the same thread is part of the
intentional API contract. It can mask accidental recursion and does not solve
cross-thread deadlock. Track the invariant across the full outermost lock
scope.

## Waiting for state

Use `AtomicCondition` when a generation notification plus an independently
stored predicate is sufficient. Use a semaphore when each signal represents a
counted unit of capacity/work. Neither replaces a queue’s own storage and
shutdown protocol.

