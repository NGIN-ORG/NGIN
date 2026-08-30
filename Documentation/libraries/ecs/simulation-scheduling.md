---
title: Simulation and scheduling
description: Coordinate update phases, fixed steps, resources, events, dependencies, and parallel execution.
---

# Simulation and scheduling

`Simulation` owns the high-level frame model above world storage and system
execution.

## Frame flow

```text
frame input
    │
fixed-step work (zero or more iterations)
    │
update schedule
    │
events and frame completion
```

Set `FixedDeltaTime` to zero when the application supplies only variable-step
frames. Choose a positive fixed interval for deterministic simulation work that
may need multiple iterations during a long display frame.

## Schedules

Schedules group named work by phase. Access declarations and explicit
dependencies form a graph that the executor can validate and run.

## Parallel execution

Parallel execution is opt in. It is safe only when system access accurately
describes every shared read and write. Hidden mutation outside declared
components or resources invalidates the scheduler's conflict analysis.

## Determinism

Fixed time steps help but do not by themselves guarantee deterministic output.
Ordering, external input, floating-point behavior, random sources, and
parallel reductions must also have deliberate contracts.
