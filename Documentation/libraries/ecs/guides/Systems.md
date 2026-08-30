# Systems And Scheduling

A system is a named piece of work. A `Schedule` reads its parameter types,
figures out dependencies, and runs compatible systems together.

Start with `Each` for per-entity logic. It can also read shared resources while
iterating. Use `System` when you need an explicit query, commands, mutable
resource coordination, local state, or events.

## Per-Entity Logic With `Each`

```cpp
SystemId move = update.Each<Without<Frozen>>(
    "Move",
    [](Position& position,
       const Velocity& velocity,
       EntityId entity,
       const FrameInfo& frame) {
        position.X += velocity.X * static_cast<float>(frame.DeltaTime);
    });
```

`Each` infers the data contract:

| Parameter | Declared access |
| --- | --- |
| `T&` | write component `T` |
| `const T&` | read component `T` |
| `OptionalComponent<T>` | optional read of `T` |
| `Resource<T>` / `Resource<const T>` | write/read shared resource `T` |
| `EntityId` | current entity |
| `FrameInfo`, `FixedStepInfo`, `RunContext` | execution context |

Filters such as `With<Player>` and `Without<Frozen>` go in the template list.

```cpp
update.Each(
    "Tint damaged ships",
    [](Color& color, OptionalComponent<Health> health) {
        if (health && health->Value < 25)
        {
            color = Color::Red;
        }
    });
```

Use a concrete lambda signature. Generic lambdas and overloaded call operators
are ambiguous, so NGIN.ECS rejects them and points you toward an explicit query.

## Systems With More Context

```cpp
struct Rules { int MaxEnemies; };
struct SpawnState { double Cooldown = 0.0; int Wave = 0; };
struct WaveStarted { int Number; };

update.System(
    "Spawn enemies",
    [](Commands& commands,
       Resource<const Rules> rules,
       Local<SpawnState> state,
       EventWriter<WaveStarted> events,
       const FrameInfo& frame) {
        state->Cooldown -= frame.DeltaTime;
        if (state->Cooldown <= 0.0 && rules->MaxEnemies > 0)
        {
            for (int i = 0; i < rules->MaxEnemies; ++i)
            {
                (void)commands.Spawn(Enemy {}, Health {100});
            }
            events.Send(WaveStarted {++state->Wave});
            state->Cooldown = 1.0;
        }
    });
```

Supported parameters are:

- `Query<Terms...>&`
- `Commands&`
- `Resource<T>` or `Resource<const T>`
- `Local<T>`
- `EventReader<T>`, `EventWriter<T>`, or `ImmediateEventWriter<T>`
- `const FrameInfo&`, `const FixedStepInfo&`, or `const RunContext&`
- `ExclusiveWorld`

Resources must be inserted into the simulation before a system requests them.
Each `Local<T>` instance belongs to one registered system and survives between
runs.

## Order Only What Matters

Data conflicts already create safe stages. Add an explicit edge when the game
rule—not just memory safety—requires an order:

```cpp
const SystemId move = update.Each("Move", moveShips);
const SystemId collide = update.System("Collide", findCollisions);

update.Before(move, collide);
// Equivalent: update.After(collide, move);
```

Keep the returned `SystemId`; it remains stable. For larger graphs, create
groups with `CreateGroup`, add systems with `AddToGroup`, then order groups with
`BeforeGroup` or `AfterGroup`.

Cycles are reported when the schedule builds. Registration order breaks ties
between otherwise equal choices.

## Skip Work With Conditions

```cpp
update.RunIf(animate, [](const RunContext& context) {
    return context.Frame.DeltaTime > 0.0;
});
```

`SetRunCondition` gates the whole schedule. A skipped system does not advance
its change-detection baseline, so it can still observe everything that changed
while it was asleep.

## Choose An Executor

Set the mode once when constructing `Simulation`:

```cpp
Simulation game {
    SimulationConfig {
        .Execution = ExecutionMode::DeterministicParallel}};
```

| Mode | Best for |
| --- | --- |
| `Serial` | simplest debugging and deterministic reference behavior |
| `Parallel` | maximum throughput where independent-system order is irrelevant |
| `DeterministicParallel` | parallel work with stable deferred-command merge order |

The schedule separates systems that conflict on components, resources, events,
commands, or explicit ordering. `MainThreadSystem` always runs on the caller
thread. `ExclusiveSystem` receives `ExclusiveWorld` and runs alone.

Deterministic parallel execution stabilizes graph choices and command order; it
does not make floating-point results identical across compilers or platforms.

### Scale One `Each` Across Chunks

Independent systems are only one source of parallel work. For a large dense
loop, explicitly let one `Each` use several chunks at once:

```cpp
const SystemId integrate = update.Each(
    "Integrate",
    [](Position& position, const Velocity& velocity) {
        position.X += velocity.X;
        position.Y += velocity.Y;
    });

update.SetIterationPolicy(
    integrate,
    IterationPolicy::ParallelChunks,
    4096); // use the serial path below this row count
```

`ParallelChunks` is available only for `Each`. The executor owns a persistent
pool; `SimulationConfig::ParallelWorkerCount` is the number of background
workers, while zero selects `hardware_concurrency - 1`. Small workloads fall
back to the serial loop.

The same callable object can run concurrently, so mutable captures must be
thread-safe. Each worker writes separate chunks, but captured globals and
external containers are still your responsibility. Resources are resolved once
per run rather than once per entity. A mutable `Resource<T>` is shared by every
chunk, so synchronize it or prefer an immutable `Resource<const T>`.
Structural commands and events are intentionally not parameters of `Each`.

## See What The Schedule Built

```cpp
update.Build();
std::cout << update.DiagnosticsText();

const std::string json = update.DiagnosticsJson();
const std::string dot = update.DiagnosticsDot();
```

Diagnostics expose stages, inferred access, ordering reasons, conditions,
command counts, failures, and last successful change versions. Per-run timings
are populated when `RunContext::Diagnostics` is rolling or trace. JSON is
versioned for tools; DOT can be rendered with Graphviz.

For a fully manual loop, see [Manual execution](Simulation.md#manual-execution).
For failure behavior, see [Safety rules](ErrorsAndThreading.md).
