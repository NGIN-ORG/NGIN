---
title: Mutexes and lock guards
description: Code reference for exclusive, recursive, shared, read-write, spin, ticket locks, and scope guards.
---

# Mutexes and lock guards

**Headers:** focused files under `<NGIN/Sync/...>`  
**Namespace:** `NGIN::Sync`  
**Target:** `NGIN::Base::Foundation`

## `Mutex` and `RecursiveMutex`

```cpp
void Lock();
bool TryLock();
void Unlock();

void lock();
bool try_lock();
void unlock();
```

`Mutex` is non-recursive. `RecursiveMutex` permits the owning thread to acquire
multiple times and requires matching releases. Both are non-copyable.

## `SharedMutex`

```cpp
void Lock();
bool TryLock();
void Unlock();
void LockShared();
bool TryLockShared();
void UnlockShared();
```

Standard exclusive and shared lowercase aliases are also provided.
`ReadWriteLock` exposes the same behavior as `StartRead`, `EndRead`,
`TryStartRead`, `StartWrite`, `EndWrite`, and `TryStartWrite`, plus standard
lock names.

No fairness or upgrade/downgrade operation is promised. Releasing must match
the acquisition mode.

## `SpinLock` and `TicketLock`

Both satisfy Lockable operations. `SpinLock` uses an atomic flag with acquire
on successful lock and release on unlock, yielding with bounded backoff while
contended. `TicketLock` assigns FIFO-style tickets and advances the serving
counter on unlock.

They busy-wait and are suitable only for bounded non-blocking critical
sections. They do not make blocking or callback code safe.

## Guards

```cpp
template<BasicLockableConcept T>
class LockGuard;

template<SharedLockableConcept T>
class SharedLockGuard;
```

Construction acquires. Destruction releases if still owned. Guards are
non-copyable; move construction transfers ownership. The referenced lock must
outlive the guard, and guard lifetime must not outlive the thread/execution
ownership rules of the underlying lock.

## Source

[Browse mutex declarations](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Sync).

