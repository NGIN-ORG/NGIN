---
title: Scheduler classes
description: Code reference for InlineScheduler, CooperativeScheduler, ThreadPoolScheduler, and FiberScheduler.
---

# Scheduler classes

All scheduler classes implement the surface required by `ExecutorRef`:

```cpp
ScheduleResult Execute(WorkItem item) noexcept;
ScheduleResult ExecuteAt(WorkItem item, Time::TimePoint resumeAt) noexcept;
```

## `InlineScheduler`

**Header:** `<NGIN/Execution/InlineScheduler.hpp>`  
**Defined:** [`InlineScheduler.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/InlineScheduler.hpp#L17)

Immediate submission invokes synchronously. Timed submission sleeps the caller
until the deadline, then invokes. `RunOne` returns `false`; `RunUntilIdle` and
`CancelAll` do nothing.

## `CooperativeScheduler`

**Header:** `<NGIN/Execution/CooperativeScheduler.hpp>`  
**Defined:** [`CooperativeScheduler.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/CooperativeScheduler.hpp#L22)

```cpp
bool RunOne();
bool RunOneAt(Time::TimePoint now);
void RunUntilIdle();
void RunUntilIdleAt(Time::TimePoint now);
std::size_t PendingReady() const noexcept;
std::size_t PendingTimers() const noexcept;
```

The owner supplies progress. `RunOneAt` invokes at most one due timer, otherwise
one ready item. The implementation is intended for owner-thread/caller-driven
use, not an implicit concurrent multi-producer queue.

## `ThreadPoolScheduler`

**Header:** `<NGIN/Execution/ThreadPoolScheduler.hpp>`  
**Defined:** [`ThreadPoolScheduler.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/ThreadPoolScheduler.hpp#L27)

```cpp
explicit ThreadPoolScheduler(std::size_t threadCount = HardwareConcurrency());
~ThreadPoolScheduler();
bool RunOne() noexcept;
void RunUntilIdle() noexcept;
void CancelAll() noexcept;
void SetPriority(int) noexcept;
void SetAffinity(std::uint64_t) noexcept;
```

Zero threads are normalized to one. Construction owns workers and a timer
thread. Destruction publishes stop, wakes and joins threads, then clears queues.
`CancelAll` discards queued/timed work without interrupting running work.

## `FiberScheduler`

**Header:** `<NGIN/Execution/FiberScheduler.hpp>`  
**Defined:** [`FiberScheduler.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/FiberScheduler.hpp#L31)

```cpp
FiberScheduler(std::size_t numThreads = 4, std::size_t numFibers = 128);
~FiberScheduler();
bool RunOne() noexcept;          // always false; workers own progress
void RunUntilIdle() noexcept;   // no-op
void CancelAll() noexcept;
```

Jobs run on reusable stackful fibers owned by worker threads. The build must
support stackful fibers. Destruction stops, cancels queues, wakes and joins
workers/driver, then releases sleeping tasks.

## Common scheduler contract

Accepted deadlines use monotonic time and mean “not before.” Policy hints and
task notification hooks do not promise platform scheduling changes or metrics
unless the concrete implementation documents them.

