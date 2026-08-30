---
title: Deadlines, sleep, and testing
description: Carry timeout budgets correctly, understand blocking SleepFor, and keep time-based tests deterministic.
---

# Deadlines, sleep, and testing

A relative timeout says how long to wait starting now. An absolute deadline
says when the complete operation expires. Across resolution, connection,
handshake, and request stages, pass one deadline or remaining budget; applying
the full relative timeout at each stage multiplies total wait time.

`Time::SleepFor(unit)` converts a positive time-dimension unit to nanoseconds,
rounding upward, then blocks the calling thread. Non-positive durations return
without waiting. Platform scheduling can wake later than requested; sleep is
not a precision timer or a guaranteed exact delay.

Do not call blocking `SleepFor` on a cooperative scheduler thread. Use the
delay/yield facility on `TaskContext` or the owning runtime so other operations
can progress.

## Testing

Code that owns retry/backoff/expiry policy should depend on an injected clock or
explicit “now” value. A test can then advance time deterministically instead of
sleeping and hoping the operating system schedules within a narrow interval.

Test boundary cases:

- exactly at the deadline;
- zero/negative duration;
- very small fractional units and rounding;
- maximum representable deadline/overflow;
- cancellation during a wait;
- late wake-up and remaining-budget calculation.

Wall-clock timestamps require a different calendar/time-zone facility. Do not
reinterpret `TimePoint` to fill that role.
