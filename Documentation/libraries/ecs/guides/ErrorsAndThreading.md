# Safety Rules And Failure Behavior

NGIN.ECS keeps the common path lightweight by enforcing a few boundaries. If
you remember the following rules, most lifetime and threading bugs disappear.

## The Five Rules

1. Treat entity IDs as world-local handles.
2. Do not keep query rows, spans, pointers, or references after the callback.
3. Use `Commands` for structural changes during systems and queries.
4. Declare every system read and write through its parameters.
5. Do not access one world concurrently outside a schedule executor.

## Absence Versus Mistakes

Use non-throwing checks when absence is expected:

```cpp
world.IsAlive(entity);
world.Has<Health>(entity);
world.TryGet<Health>(entity);
world.TryGetMutable<Health>(entity);
world.Remove<Health>(entity); // false when the component is absent
```

Required operations such as `Get`, `GetMutable`, `Add`, and `Set` throw standard
exceptions for stale entities, missing components, duplicates, or invalid
inputs. Missing resources fail the system run with an actionable message.

Ordering cycles and mutation during active iteration are logic errors.

## Reference Invalidation

Spawn, despawn, add, remove, and clear may move storage. Assume they invalidate
all pointers, references, rows, chunks, and spans obtained from the world.

Query-derived access is always callback-scoped—even if you know the current
operation does not relocate anything. Store `EntityId` when you need to refer to
an entity later, then look its component up again.

## Query Nesting

Read-only queries may be nested. A mutable query cannot begin while another
query is active. Direct structural mutation is rejected during any active
query.

## Threading

Direct concurrent access to one `World` is unsupported. Parallel executors are
the safe parallel entry point: they use the schedule's declared component,
resource, and event access to run only non-conflicting systems together.

Do not hide world access in globals, capture undeclared component references,
or retain system parameters after invocation. The scheduler cannot protect
access it cannot see.

Use `MainThreadSystem` for caller-thread APIs such as many render or windowing
backends. Use `ExclusiveSystem` when a task genuinely needs unrestricted world
access.

`ParallelExecutor` and `DeterministicParallelExecutor` keep their worker
threads alive between runs. `IterationPolicy::ParallelChunks` may invoke the
same `Each` callable concurrently on disjoint chunks. Do not use unsynchronized
mutable captures. When one task fails, the pool drains every already-eligible
task before the exception escapes.

## What Commits When Work Fails?

NGIN.ECS does not roll a frame back.

- In serial mode, a failed system stops later systems.
- In a parallel stage, already-running peers finish.
- Successful direct component writes remain committed.
- Successful systems advance their change baselines.
- Every unflushed command buffer in a failed parallel stage is discarded.
- `ExecutionError::Committed` reports completed systems, applied commands, and
  the change version.
- `Simulation::LastStepResult()` reports clocks and work committed before a
  failed step escaped.

A direct `Commands::Flush` has its own
[completed-prefix guarantee](Commands.md#if-a-command-fails).

Parallel execution can make the relative timing of independent systems
observable. Deterministic-parallel mode stabilizes scheduling choices and
deferred-command merge order, but cross-platform floating-point identity is not
promised.
