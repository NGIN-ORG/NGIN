# Migrate From 0.2 To 0.3

Version 0.3 replaces the old scheduler contract. There is no compatibility
header or alias, so migration mistakes fail at compile time instead of changing
behavior silently.

## Fast Checklist

- Replace `Scheduler` with `Schedule` or, preferably, `Simulation`.
- Replace `AddSystem` with `System` or `Each`.
- Replace `AddExclusiveSystem` with `ExclusiveSystem`.
- Move frame/tick ownership into `Simulation::Step`.
- Rename tick APIs to change-version APIs.
- Give manual `Schedule::Run` an executor and `RunContext`.
- Use typed resources, local state, and events instead of hidden captures.

## Normal Game Loop

Before:

```cpp
World world;
Scheduler scheduler;
scheduler.AddSystem("Move", callable);
scheduler.Run(world);
```

After:

```cpp
Simulation game;
game.Schedule(Update).Each("Move", callable);
game.Step(FrameInfo {.DeltaTime = deltaSeconds});
```

`Simulation` now owns the world, schedules, executor, resources, events, fixed
accumulator, frame index, simulation tick, and change boundaries.

Use `Each` for ordinary per-entity functions:

```cpp
game.Schedule(Update).Each(
    "Move",
    [](Position& position, const Velocity& velocity) {
        position.X += velocity.X;
    });
```

Use `System` for explicit queries, commands, resources, local state, or events.

## Manual Loop

```cpp
World world;
Schedule schedule;
SerialExecutor executor;

world.AdvanceChangeVersion();
RunContext context {
    .ChangeVersion = world.CurrentChangeVersion()};

schedule.Run(world, executor, context);
```

`Schedule::Run` deliberately advances nothing. Your engine owns every boundary
on this path.

API renames:

| 0.2 | 0.3 |
| --- | --- |
| `CurrentTick()` | `CurrentChangeVersion()` |
| `PreviousTick()` | `PreviousChangeVersion()` |
| `AdvanceTick()` | `AdvanceChangeVersion()` |

## Behavior To Recheck

- One schedule run is no longer assumed to be one frame.
- `Startup`, each fixed substep, and variable update have explicit change
  boundaries; `Update` and `PostUpdate` share one.
- Change filters use each system's last successful run as their baseline.
- Skipped and failed systems keep their old baseline.
- Parallel failures finish running peers and discard unflushed commands for
  that stage.
- Deterministic-parallel mode merges commands in stable stage/system order.
- `OptionalComponent<T>` is the optional read parameter for `Each`.
- Query chunk spans and cached plans are available without a separate query API.
- `EventWriter<T>` publishes at the next boundary;
  `ImmediateEventWriter<T>` publishes in the current one and creates a barrier.

The new model is explained progressively in [Quick start](QuickStart.md),
[Systems](Systems.md), and [Simulation](Simulation.md).
