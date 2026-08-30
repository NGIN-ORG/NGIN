---
title: Monotonic time points
description: Understand TimePoint's clock domain, ordering, raw nanoseconds, and relationship to units.
---

# Monotonic time points

`MonotonicClock::Now()` returns `TimePoint`, an opaque value containing unsigned
nanoseconds since an unspecified monotonic epoch.

`TimePoint` supports equality and ordering. `ToNanoseconds()` exposes its tick
for arithmetic; `FromNanoseconds()` reconstructs a point in the same assumed
clock domain.

## Valid uses

- measuring elapsed time within one running clock domain;
- constructing scheduler and I/O deadlines;
- ordering events observed by that clock;
- timeout budget accounting.

## Invalid assumptions

- the raw number is not Unix time;
- it is not a date or civil timestamp;
- it need not be meaningful after reboot or in another process/machine;
- converting it to text does not create a portable timestamp.

Unsigned subtraction assumes the later point is actually later. Compare first
when inputs are not ordered by construction. Addition for far-future deadlines
must consider integer overflow and saturate/reject according to the owning API.

NGIN.Units duration types provide explicit scale. `TimePoint` itself stays a
minimal monotonic tick value used by low-level schedulers. Convert duration
units once at the boundary, with explicit rounding, instead of passing raw
integers named only by convention.
