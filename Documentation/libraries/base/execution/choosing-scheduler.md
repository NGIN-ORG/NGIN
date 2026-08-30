---
title: Choosing a scheduler
description: Select an NGIN.Execution scheduler by progress, re-entrancy, threading, timer, and shutdown requirements.
---

# Choosing a scheduler

Choose a scheduler by who must make progress and what concurrency callers can
observe. Thread count is a secondary decision.

## Decision table

| Requirement | Choose | Consequence |
| --- | --- | --- |
| Submission must finish the callback synchronously | `InlineScheduler` | The callback can re-enter the caller before `Execute` returns |
| One existing loop owns all execution | `CooperativeScheduler` | The loop must pump ready work and timers |
| Background parallel work should progress independently | `ThreadPoolScheduler` | Callbacks may run concurrently and need synchronization |
| Stackful synchronous code must cooperatively suspend | `FiberScheduler` | Platform/build fiber support and stack lifetime become requirements |

## Inline scheduler

`Execute` invokes immediately. `ExecuteAt` blocks the submitting thread until
the monotonic deadline, then invokes. There is no queue to drain or cancel.

Use it for adapters and tests where synchronous semantics are intentional. Do
not inject it merely to “make async code simpler”: immediate continuation can
change lock ordering and object lifetime assumptions.

## Cooperative scheduler

The owner thread provides progress with `RunOne`, `RunOneAt`,
`RunUntilIdle`, or `RunUntilIdleAt`. Its queue is not a general concurrent
multi-producer contract; confine submission and driving according to the
owning loop’s policy.

The `At` variants are especially useful in tests because they make due timers
independent of wall-clock sleeps.

## Thread-pool scheduler

Construction starts at least one worker and a timer thread. Submissions from a
worker prefer that worker’s local queue; external submissions use the injection
queue. Work stealing distributes available jobs. `RunOne` also lets the
calling thread execute one available item, but normal progress does not require
pumping.

Callbacks can overlap. Capturing the same object in two jobs requires
confinement, atomics, or locks. The pool does not infer data dependencies.

## Fiber scheduler

Workers run jobs on reusable stackful fibers. `Fiber::YieldNow` returns to the
worker’s main fiber without blocking its OS thread. Use it only when the build
supports fibers and synchronous call stacks genuinely need cooperative yield.
Coroutine tasks usually need only an executor and do not require stackful
fibers.

## Library boundaries

Reusable code should generally accept `ExecutorRef` or `TaskContext`, not
construct a private pool. This keeps thread ownership, shutdown, testing, and
resource limits with the application.

Document whether callbacks may run inline, concurrently, and after the
initiating call returns. An executor abstraction hides the concrete type, not
those observable semantics.

