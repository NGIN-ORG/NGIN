---
title: NGIN.Execution API reference
description: Symbol-level reference for executors, work items, schedulers, threads, fibers, and submission failures.
---

# NGIN.Execution API reference

**Component target:** `NGIN::Base::Execution`  
**Umbrella:** `<NGIN/Execution.hpp>`  
**Namespace:** `NGIN::Execution`

This is code reference. If these types are new to you, start with
[Learn NGIN.Execution](../../../libraries/base/execution.md).

## Submission model

| Symbol | Header | Purpose |
| --- | --- | --- |
| [`ExecutorRef`](./execution/executor-ref.md) | `<NGIN/Execution/ExecutorRef.hpp>` | Borrowed type-erased immediate/delayed executor |
| [`WorkItem`](./execution/work-item.md) | `<NGIN/Execution/WorkItem.hpp>` | Move-only callable or coroutine work payload |
| `ScheduleError` | `<NGIN/Execution/ScheduleResult.hpp>` | Exact submission rejection kind |
| `ScheduleResult` | `<NGIN/Execution/ScheduleResult.hpp>` | `std::expected<void, ScheduleError>` |

## Schedulers

| Symbol | Progress owner | Reference |
| --- | --- | --- |
| `InlineScheduler` | Submitting thread | [Scheduler classes](./execution/schedulers.md#inlinescheduler) |
| `CooperativeScheduler` | Caller of pump methods | [Scheduler classes](./execution/schedulers.md#cooperativescheduler) |
| `ThreadPoolScheduler` | Owned workers/timer thread | [Scheduler classes](./execution/schedulers.md#threadpoolscheduler) |
| `FiberScheduler` | Owned workers running fibers | [Scheduler classes](./execution/schedulers.md#fiberscheduler) |

## Native execution resources

| Symbol | Header | Reference |
| --- | --- | --- |
| `Thread`, `WorkerThread`, `Thread::Options` | `<NGIN/Execution/Thread.hpp>` | [Thread](./execution/thread.md) |
| `ThreadName` | `<NGIN/Execution/ThreadName.hpp>` | [Thread](./execution/thread.md#threadname) |
| `ThisThread` functions | `<NGIN/Execution/ThisThread.hpp>` | [Thread](./execution/thread.md#thisthread) |
| `Fiber`, `FiberOptions`, `FiberResumeResult` | `<NGIN/Execution/Fiber.hpp>` | [Fiber](./execution/fiber.md) |
| `ThisFiber` functions | `<NGIN/Execution/ThisFiber.hpp>` | [Fiber](./execution/fiber.md#thisfiber) |

## Contract summary

- Schedulers own accepted `WorkItem` values.
- `ExecutorRef` borrows its scheduler and never extends lifetime.
- `ExecuteAt` means no earlier than a monotonic deadline.
- Callables invoked by `WorkItem::Invoke()` must not throw.
- Pool/fiber scheduler destruction stops, joins, and discards remaining queued
  work; it does not forcibly interrupt already running code.
- Thread/fiber handles are move-only resource owners.

## Source tree

[Browse public Execution declarations](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution).

