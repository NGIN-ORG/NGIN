# API Cheat Sheet

This page is a memory aid, not a tutorial. Follow the links beside each section
for the reasoning and examples.

## Simulation

```cpp
Simulation game {SimulationConfig {
    .Execution = ExecutionMode::Serial,
    .FixedDeltaTime = 1.0 / 60.0,
    .MaxFixedStepsPerFrame = 8}};

EntityId entity = game.Spawn(components...);
auto entities = game.SpawnBatch(count, componentPrototypes...);
game.Despawn(entity);
game.Has<T>(entity);
game.Get<T>(entity);
game.GetMutable<T>(entity);

Schedule& update = game.Schedule(Update);
StepResult result = game.Step(FrameInfo {.DeltaTime = seconds});
World& world = game.GetWorld();
```

Default labels: `Startup`, `FixedUpdate`, `Update`, `PostUpdate`.

Other operations: `InsertResource`, `ContainsResource`, `GetResource`,
`SendEvent`, `CreateSchedule`, `ScheduleName`, `Pipeline`, `SetPipeline`,
`ResetStartup`, `FrameIndex`, `SimulationTick`, `FixedAccumulator`, and
`LastStepResult`.

See [Simulation](Simulation.md).

## World

```cpp
EntityId Spawn(Cs&&... components);
Vector<EntityId> SpawnBatch(count, const Cs&... prototypes);
void Despawn(EntityId);
void DespawnBatch(span<const EntityId>);
void ReserveEntities(count);
void ReserveArchetype<Components...>(count);
bool IsAlive(EntityId) const;
UInt64 AliveCount() const;
void Clear();

bool Has<T>(EntityId) const;
const T* TryGet<T>(EntityId) const;
T* TryGetMutable<T>(EntityId);
const T& Get<T>(EntityId) const;
T& GetMutable<T>(EntityId);
void Add<T>(EntityId, T value);
bool Remove<T>(EntityId);
void Set<T>(EntityId, T value);
void MarkChanged<T>(EntityId);

UInt64 CurrentChangeVersion() const;
UInt64 PreviousChangeVersion() const;
void AdvanceChangeVersion();
UInt64 SchemaGeneration() const;
WorldStats Stats() const;
```

See [Entities](Entities.md) and [Change detection](ChangeDetection.md).

## Queries

```cpp
Query<Write<Position>, Read<Velocity>, Without<Frozen>> query {world};

query.ForEach([](auto row) {
    EntityId entity = row.Entity();
    Position& position = row.template Get<Position>();
});

query.ForEachChunk([](const auto& chunk) {
    auto entities = chunk.Entities();
    auto positions = chunk.template WriteSpan<Position>();
    auto velocities = chunk.template ReadSpan<Velocity>();
});
```

Terms: `Read<T>`, `Write<T>`, `Optional<T>`, `With<T>`, `Without<T>`,
`Added<T>`, `Changed<T>`.

Row access: `Entity`, `Get<T>`, `TryGet<T>`, `MarkChanged<T>`.

Chunk access: `Count`, `Entities`, `EntityAt`, `Has<T>`, `Get<T>`, `TryGet<T>`,
`ReadSpan<T>`, `WriteSpan<T>`, `MarkChanged<T>`.

See [Queries](Queries.md).

## Commands

```cpp
DeferredEntity pending = commands.Spawn(components...);
commands.Despawn(entityOrPending);
commands.Add<T>(entityOrPending, value);
commands.Remove<T>(entityOrPending);
commands.Set<T>(entityOrPending, value);
commands.Size();
commands.Reserve(operationCount);
commands.Clear();
commands.Flush(world); // manual ownership only
```

See [Commands](Commands.md).

## Schedule

```cpp
SystemId a = schedule.Each("Move", callable);
SystemId b = schedule.System("Resolve", callable);
SystemId c = schedule.MainThreadSystem("Present", callable);
SystemId d = schedule.ExclusiveSystem("Reset", callable);

schedule.Before(a, b);
schedule.After(b, a);
schedule.RunIf(a, condition);
schedule.SetRunCondition(condition);
schedule.SetIterationPolicy(a, IterationPolicy::ParallelChunks, minimumRows);
schedule.Build();
```

Groups: `CreateGroup`, `AddToGroup`, `BeforeGroup`, `AfterGroup`.

Manual execution:

```cpp
SerialExecutor executor;
RunResult result = schedule.Run(world, executor, context);
```

Executors: `SerialExecutor`, `ParallelExecutor`,
`DeterministicParallelExecutor`.

System parameters:

- `Query<Terms...>&`
- `Commands&`
- `Resource<T>` / `Resource<const T>`
- `Local<T>`
- `EventReader<T>` / `EventWriter<T>` / `ImmediateEventWriter<T>`
- `const FrameInfo&` / `const FixedStepInfo&` / `const RunContext&`
- `ExclusiveWorld`

`Each` additionally accepts component references, `OptionalComponent<T>`, and
`EntityId`. Filters are template terms, such as
`Each<Without<Frozen>>(name, callable)`.

See [Systems](Systems.md).

## Diagnostics

After `Build` or `Run`, inspect `SystemCount`, `StageCount`, stage membership,
component/resource/event access, explicit ordering, compiled constraints,
condition results, durations, command counts, failures, and last successful
change versions.

Export with:

```cpp
schedule.DiagnosticsText();
schedule.DiagnosticsJson(); // schema version 1
schedule.DiagnosticsDot();
```

For bounded runtime profiles, configure `SimulationConfig::Diagnostics`, then
read `Profiles()` or `LastFrameProfile()`. `ExportChromeTrace()` emits Chrome
Trace JSON schema version 1.

## Headers

Use `<NGIN/ECS/ECS.hpp>` for everything. Supported focused headers include:

```text
Entity.hpp                 Query.hpp
World.hpp                  Commands.hpp
Schedule.hpp               Simulation.hpp
SystemParameters.hpp       TypeRegistry.hpp
Diagnostics.hpp
```

`NGIN/ECS/Detail/*` is private implementation surface.
