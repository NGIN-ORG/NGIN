---
title: NGIN.Sync API reference
description: Symbol-level reference for mutexes, shared locks, spin locks, guards, semaphores, and atomic conditions.
---

# NGIN.Sync API reference

**Umbrella:** `<NGIN/Sync.hpp>`  
**Namespace:** `NGIN::Sync`  
**Target:** `NGIN::Base::Foundation`

If these primitives are new to you, start with [Learn NGIN.Sync](../../../../libraries/base/synchronization.md).

## Lock types

| Symbol | Header | Reference |
| --- | --- | --- |
| `Mutex`, `RecursiveMutex` | matching focused headers | [Mutexes](./mutexes.md) |
| `SharedMutex`, `ReadWriteLock` | matching focused headers | [Mutexes](./mutexes.md#sharedmutex) |
| `SpinLock`, `TicketLock` | matching focused headers | [Mutexes](./mutexes.md#spinlock-and-ticketlock) |
| `LockGuard<T>`, `SharedLockGuard<T>` | `<NGIN/Sync/LockGuard.hpp>` | [Mutexes](./mutexes.md#guards) |
| [`Semaphore<MaxCount>`](./semaphore.md) | `<NGIN/Sync/Semaphore.hpp>` | Counted permits |
| [`AtomicCondition`](./atomic-condition.md) | `<NGIN/Sync/AtomicCondition.hpp>` | Generation-based wait/notify |

All mutex-like types are non-copyable and provide standard Lockable-compatible
lowercase names where their capabilities apply.

## Concepts

`Concepts.hpp` defines `BasicLockableConcept`, `LockableConcept`, and
`SharedLockableConcept` used by generic guards and compatible user types.

## Source tree

[Browse public Sync declarations](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Sync).

