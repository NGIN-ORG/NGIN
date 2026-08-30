---
title: Learn NGIN.Time
description: Use monotonic points, explicit duration units, deadlines, and sleep without inventing wall-clock semantics.
---

# Learn NGIN.Time

NGIN.Time is intentionally small. It provides an opaque monotonic time point,
the platform monotonic clock, and unit-aware sleeping. It does not provide
calendar dates, time zones, or persistent timestamps.

## Start here

1. Understand [monotonic points and duration units](./time/monotonic-time.md).
2. Apply [deadlines, sleeping, overflow, and test policy](./time/deadlines-sleep.md).
3. Look up declarations in the [Time API](../../reference/cpp/base/time.md).

## Measure elapsed time

```cpp
#include <NGIN/Time.hpp>

const auto start = NGIN::Time::MonotonicClock::Now();
DoWork();
const auto finish = NGIN::Time::MonotonicClock::Now();

const NGIN::UInt64 elapsedNanoseconds =
    finish.ToNanoseconds() - start.ToNanoseconds();
```

`TimePoint` stores nanoseconds since an unspecified monotonic epoch. It is
ordered inside that clock domain. The raw number has no calendar meaning and
must not be persisted or compared across machines/process domains.

## Sleep using units

```cpp
NGIN::Time::SleepFor(NGIN::Units::Milliseconds{2.5});
```

`SleepFor` accepts a time-dimension unit and rounds positive fractional
durations up to nanoseconds before using the platform wait. Sleeping blocks the
calling thread. Coroutine code should use its scheduler/context delay operation
instead so the executor can run other work.

Prefer an absolute deadline across multi-stage operations. Reapplying the full
relative timeout at each stage can exceed the caller's intended budget.
