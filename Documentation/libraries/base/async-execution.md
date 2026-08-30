---
title: Async and execution
description: Work with cold tasks, cancellation, schedulers, threads, fibers, and synchronization.
---

# Async and execution

NGIN.Base separates asynchronous values from the runtime that executes them.
There is no required process-global scheduler.

## Async model

- `Task<T>` represents cold coroutine work.
- `TaskContext` supplies execution and cancellation interaction.
- spawning transfers a task to an execution owner;
- detaching is explicit because it changes observation and lifetime behavior;
- synchronous waiting is available at deliberate blocking boundaries.

## Execution model

Execution facilities cover cooperative schedulers, task runtimes, threads,
fibers, and runtime lanes. Choose the smallest runtime that matches the
application's concurrency model.

```text
Task<T> ──spawn──► scheduler/runtime ──► completion
   │                    │
   └── cancellation ◄───┘
```

## Synchronization

The Sync area provides primitives for coordinating work. Prefer ownership and
message boundaries that avoid shared mutable state; use synchronization when
the shared state is intentional.

## Operational rules

- Never assume creating a task starts it.
- Propagate cancellation through the supplied context.
- Define which runtime owns detached work and how shutdown drains it.
- Avoid blocking a cooperative execution lane with an unrelated synchronous
  operation.
