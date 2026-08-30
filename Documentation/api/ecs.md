---
title: NGIN.ECS API
description: World, entities, components, queries, commands, systems, schedules, executors, simulation, events, resources, and diagnostics.
---

# NGIN.ECS API

**Include:** `<NGIN/ECS/ECS.hpp>`  
**Package:** `NGIN.ECS`  
**Namespace:** `NGIN::ECS`

## Simulation

`Simulation` is the high-level owner for a world, default schedules, resources,
events, frame/fixed-step state, and diagnostics.

```cpp
Simulation game {SimulationConfig {
    .Execution = ExecutionMode::Serial,
    .FixedDeltaTime = 1.0 / 60.0,
    .MaxFixedStepsPerFrame = 8}};

EntityId entity = game.Spawn(Position {}, Velocity {});
Schedule& update = game.Schedule(Update);
StepResult result = game.Step(FrameInfo {.DeltaTime = seconds});
```

Default schedule labels are `Startup`, `FixedUpdate`, `Update`, and
`PostUpdate`. `Startup` runs once until `ResetStartup`. Fixed update can run
several bounded steps to consume the accumulator.

Other central operations include `SpawnBatch`, `Despawn`, `Has`, `Get`,
`GetMutable`, `InsertResource`, `ContainsResource`, `GetResource`, `SendEvent`,
`CreateSchedule`, `SetPipeline`, `FrameIndex`, `SimulationTick`,
`FixedAccumulator`, and `LastStepResult`.

## World and entities

```cpp
EntityId Spawn(Cs&&... components);
Vector<EntityId> SpawnBatch(count, const Cs&... prototypes);
void Despawn(EntityId);
bool IsAlive(EntityId) const;
UInt64 AliveCount() const;

bool Has<T>(EntityId) const;
const T* TryGet<T>(EntityId) const;
T* TryGetMutable<T>(EntityId);
const T& Get<T>(EntityId) const;
T& GetMutable<T>(EntityId);
void Add<T>(EntityId, T value);
bool Remove<T>(EntityId);
void Set<T>(EntityId, T value);
void MarkChanged<T>(EntityId);
```

`EntityId` includes generation information. An ID becomes stale after despawn;
`IsAlive` validates it. Adding or removing a component moves the entity between
archetypes and can invalidate component references. Do not keep component
pointers across structural changes.

Use `ReserveEntities` and `ReserveArchetype<Components...>` when counts are
known. `WorldStats` exposes entity/archetype/storage information for capacity
and performance diagnostics.

## Queries

```cpp
Query<Write<Position>, Read<Velocity>, Without<Frozen>> query {world};

query.ForEach([](auto row) {
    Position& position = row.template Get<Position>();
    const Velocity& velocity = row.template Get<Velocity>();
});
```

Terms are `Read<T>`, `Write<T>`, `Optional<T>`, `With<T>`, `Without<T>`,
`Added<T>`, and `Changed<T>`. Access declarations are also scheduler evidence;
do not mutate through an undeclared read or hidden global pointer.

Row access exposes `Entity`, `Get<T>`, `TryGet<T>`, and `MarkChanged<T>`.
Chunk access adds `Count`, `Entities`, `EntityAt`, `ReadSpan<T>`, and
`WriteSpan<T>` for data-oriented loops.

Query iteration must not perform immediate structural mutation. Record changes
in `Commands` and let the schedule flush them at the safe boundary.

## Commands

```cpp
DeferredEntity pending = commands.Spawn(Position {}, Velocity {});
commands.Add<Health>(pending, Health {100});
commands.Despawn(existing);
```

Commands support `Spawn`, `Despawn`, `Add`, `Remove`, `Set`, `Size`, `Reserve`,
`Clear`, and manual `Flush(world)`. A `DeferredEntity` is only a command-buffer
reference until the buffer is flushed; do not use it as a live `EntityId`.

## Systems and schedules

```cpp
SystemId move = schedule.Each("Move", moveCallable);
SystemId resolve = schedule.System("Resolve", resolveCallable);

schedule.Before(move, resolve);
schedule.RunIf(resolve, condition);
schedule.SetIterationPolicy(
    move, IterationPolicy::ParallelChunks, minimumRows);

auto built = schedule.Build();
```

Registration forms are `Each`, `System`, `MainThreadSystem`, and
`ExclusiveSystem`. Groups use `CreateGroup`, `AddToGroup`, `BeforeGroup`, and
`AfterGroup`.

System parameters include queries, commands, resources, local state, event
readers/writers, frame/fixed-step/run context, and `ExclusiveWorld`. `Each`
also accepts component references, `OptionalComponent<T>`, and `EntityId`.

`Build()` validates ordering and access constraints. A dependency cycle or
incompatible access must be fixed; registration order is not a substitute for
explicit dependencies.

Executors are `SerialExecutor`, `ParallelExecutor`, and
`DeterministicParallelExecutor`. Parallel execution is derived from declared
access and schedule constraints. Deterministic parallel execution controls
scheduling behavior but cannot make unordered external side effects
deterministic.

## Change detection

World versions include `CurrentChangeVersion`, `PreviousChangeVersion`, and
`AdvanceChangeVersion`. `Added<T>` and `Changed<T>` query terms filter using
component versions. Mutating through tracked write access marks according to
that access path; if code changes data through an exceptional lower-level path,
call `MarkChanged<T>` deliberately.

## Diagnostics

Schedules report system/stage membership, access sets, ordering constraints,
conditions, durations, command counts, failures, and last successful versions.

```cpp
schedule.DiagnosticsText();
schedule.DiagnosticsJson(); // schema version 1
schedule.DiagnosticsDot();
```

Simulation runtime profiles are bounded by `SimulationConfig::Diagnostics`.
`ExportChromeTrace()` produces Chrome Trace JSON.

## Focused headers

`Entity.hpp`, `Query.hpp`, `World.hpp`, `Commands.hpp`, `Schedule.hpp`,
`Simulation.hpp`, `SystemParameters.hpp`, `TypeRegistry.hpp`, and
`Diagnostics.hpp` are supported focused entry points. `NGIN/ECS/Detail/*` is
private.

**Source:** [`NGIN.ECS` public headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.ECS/include/NGIN/ECS)

