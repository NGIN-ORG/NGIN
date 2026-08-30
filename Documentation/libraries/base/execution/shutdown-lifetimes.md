---
title: Execution shutdown and lifetimes
description: Stop producers, cancel async work, drain ownership, and destroy schedulers without dangling executor references.
---

# Execution shutdown and lifetimes

Execution shutdown is a dependency-order problem. The object that owns a
scheduler must also know which producers, contexts, operations, and callbacks
can still reach it.

## Dependency order

```text
producer/service
      │ submits through
      ▼
 ExecutorRef / TaskContext  ──borrows──► Scheduler ──owns──► workers/queues
      │
      └─ captured state must outlive queued/running work
```

Destroy from the left toward the right only after work is terminal:

1. prevent new public operations;
2. signal producer/service shutdown;
3. request cancellation of async operations;
4. wake blocked workers or loops;
5. drive/wait for operations and running callbacks;
6. release contexts and executor references;
7. destroy the scheduler, which joins owned threads.

## What CancelAll means

For pool/fiber schedulers, `CancelAll` discards ready and timed items. It does
not interrupt a callback currently executing. A discarded raw `WorkItem` has
no automatic application completion notification. NGIN.Async retains frame
and completion machinery, but application-level cancellation still requires
the task to reach a terminal state.

For a cooperative scheduler, control queue lifetime at the owner; its public
surface is oriented around pumping and pending counts rather than a background
worker shutdown protocol.

## Avoid callbacks into destroyed state

Prefer capturing shared operation state or values. A reference capture is safe
only if the owner waits for every possible callback before destruction.
Detaching a thread or async operation does not extend arbitrary referenced
objects.

Plugins and dynamically unloaded modules need an additional rule: no queued
callable may contain code or a destructor from the module when the binary is
unloaded. Stop submissions, drain work, destroy callable storage, then unload.

## Submission during shutdown

`Stopped` is a normal race outcome once scheduler teardown begins. Treat it as
shutdown propagation, not as a reason to spin-retry. A producer that keeps
submitting after `Stopped` violates the ownership protocol and can starve
teardown.

