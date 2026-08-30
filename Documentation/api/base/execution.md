---
title: NGIN.Base Execution and Sync API
description: Executors, schedulers, threads, fibers, work items, schedule errors, and synchronization primitives.
---

# NGIN.Base Execution and Sync API

**Includes:** `<NGIN/Execution.hpp>`, `<NGIN/Sync.hpp>`  
**Target:** `NGIN::Base::Execution`  
**Namespaces:** `NGIN::Execution`, `NGIN::Sync`

Execution owns where work runs. Async owns how coroutines compose. A scheduler
can run a `WorkItem` directly and can also back an `Async::TaskContext`.

## Choose a scheduler

| Type | Progress model | Use when |
| --- | --- | --- |
| `InlineScheduler` | Runs submitted work immediately | Re-entrancy is acceptable and no queue is needed |
| `CooperativeScheduler` | Caller drives `RunOne` or `RunUntilIdle` | Tests, tools, deterministic loops, or an existing event loop own the thread |
| `ThreadPoolScheduler` | Owned worker threads plus timer thread | Independent parallel and delayed work is needed |
| `FiberScheduler` | Worker threads execute stackful fibers | Existing synchronous call chains must suspend cooperatively |

All schedulers expose the scheduler concept operations:

```cpp
ScheduleResult Execute(WorkItem item) noexcept;
ScheduleResult ExecuteAt(WorkItem item, Time::TimePoint when) noexcept;
bool RunOne();
void RunUntilIdle();
void CancelAll() noexcept;
```

Exact exception specifications vary by scheduler. Destruction stops owned
workers and releases queued work according to that scheduler's contract.

## Submission result

```cpp
enum class ScheduleError : std::uint8_t {
    InvalidExecutor,
    Stopped,
    Rejected,
    ResourceExhausted,
};

using ScheduleResult = std::expected<void, ScheduleError>;
```

Treat successful submission as acceptance, not completion. A heap-backed
`WorkItem` can allocate before `Execute` is called; the submission operation
itself is designed to remain bounded.

```cpp
auto result = scheduler.Execute([] noexcept { PerformWork(); });
if (!result) {
    ReportScheduleFailure(result.error());
}
```

## `ExecutorRef`

`ExecutorRef` is a non-owning, type-erased scheduler reference. It forwards
`Execute` and `ExecuteAt` without taking ownership of the concrete scheduler.

```cpp
NGIN::Execution::CooperativeScheduler scheduler;
NGIN::Execution::ExecutorRef executor {scheduler};
```

The concrete scheduler must outlive the reference and all accepted work that
can dispatch through it. `IsValid()` checks whether a target was bound; it does
not prove that an owned scheduler is still alive.

## Cooperative scheduling

```cpp
NGIN::Execution::CooperativeScheduler scheduler;
scheduler.Execute([] noexcept { TickOnce(); });

while (scheduler.RunOne()) {
    // The caller owns forward progress.
}
```

`RunOneAt(now)` and `RunUntilIdleAt(now)` let tests supply a clock point for
deterministic timer behavior. `RunUntilIdle()` processes ready work; it does
not wait until a future timer becomes due.

## Thread-pool scheduling

```cpp
NGIN::Execution::ThreadPoolScheduler workers {4};
auto accepted = workers.Execute([] noexcept { BuildAsset(); });
```

The pool owns its threads. `SetPriority` and `SetAffinity` request platform
settings for workers; support and exact meaning are platform-dependent. Use an
application-level barrier or async operation results when shutdown must wait
for specific user work.

## Native threads

```cpp
NGIN::Execution::Thread thread(
    [] { WorkerMain(); },
    NGIN::Execution::Thread::Options {});

if (thread.IsJoinable()) {
    thread.Join();
}
```

`Thread` is move-only. `Start`, `Join`, `Detach`, `IsJoinable`, `SetName`,
`SetAffinity`, and `SetPriority` are the main operations. `WorkerThread` wraps
the same surface with worker-oriented destruction behavior.

`ThisThread` exposes current-thread operations such as yielding, sleeping, and
hardware-concurrency discovery. `ThreadName` validates and stores the portable
name representation.

## Fibers

`Fiber` is a stackful execution context. `FiberScheduler` owns fiber stacks,
worker threads, ready work, and delayed work. `ThisFiber` exposes operations
valid from a running fiber. Do not call fiber-only suspension APIs from an
ordinary thread or coroutine that is not executing on a managed fiber.

Prefer coroutines for new asynchronous composition. Fibers are appropriate
when adapting code whose synchronous stack shape cannot yet be rewritten.

## Synchronization types

| Type | Contract |
| --- | --- |
| `Mutex` | Exclusive lock |
| `RecursiveMutex` | Exclusive lock that the owning thread may reacquire |
| `SharedMutex` / `ReadWriteLock` | Multiple readers or one writer |
| `SpinLock` | Busy-wait exclusive lock for very short critical sections |
| `TicketLock` | FIFO-style busy-wait lock |
| `Semaphore` | Counting permits |
| `AtomicCondition` | Atomic wait/notify condition used by async completion |
| `LockGuard<T>` | Scope-bound lock ownership |

Do not hold a spin lock across I/O, allocation, blocking waits, coroutine
suspension, or user callbacks. Synchronization makes access safe; it does not
define object lifetime.

## Common failures

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Work never runs on a cooperative scheduler | No code drives it | Call `RunOne`/`RunUntilIdle` from the owning loop |
| `Stopped` or `Rejected` | Submission raced shutdown or policy rejected it | Stop producers before scheduler teardown and handle the result |
| Deadlock | Waiting while holding a lock needed by the awaited work | Narrow the critical section; never suspend while holding it |
| Oversubscription | Multiple subsystems each own a large thread pool | Share an explicitly owned runtime or size pools deliberately |

**Source:** [`NGIN/Execution`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution), [`NGIN/Sync`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Sync)

