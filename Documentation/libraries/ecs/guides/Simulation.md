# Simulation, Frames, And Fixed Steps

`Simulation` is the normal home for an ECS-driven game loop. It owns one world,
the schedules that operate on it, resources, events, clocks, and an executor.

```cpp
Simulation game;

game.Schedule(Startup).System("Create level", createLevel);
game.Schedule(FixedUpdate).Each("Physics", integratePhysics);
game.Schedule(Update).Each("Animate", animate);
game.Schedule(PostUpdate).System("Report", publishResults);

while (running)
{
    game.Step(FrameInfo {.DeltaTime = frameSeconds});
}
```

## The Default Pipeline

```text
first Step only:  Startup
fixed as needed:  FixedUpdate → FixedUpdate → ...
every Step:       Update → PostUpdate
```

| Schedule | Runs | Typical work |
| --- | --- | --- |
| `Startup` | once, before the first frame | initial world setup |
| `FixedUpdate` | zero or more times per frame | physics and deterministic-rate rules |
| `Update` | once per frame | input, AI, animation, gameplay |
| `PostUpdate` | once after `Update` | extraction, reporting, cleanup |

`Update` and `PostUpdate` share one change boundary. This lets post-update work
see the final state of the frame without inventing a second frame's worth of
changes.

## Configure The Loop

```cpp
Simulation game {
    SimulationConfig {
        .Execution = ExecutionMode::DeterministicParallel,
        .ParallelWorkerCount = 0, // automatic
        .FixedDeltaTime = 1.0 / 60.0,
        .MaxFixedStepsPerFrame = 8}};
```

- Set `FixedDeltaTime` to `0.0` to disable fixed updates.
- `MaxFixedStepsPerFrame` limits catch-up work after a slow frame.
- The simulation carries unused fixed time into the next frame.

## Frame Profiling

Profiling is disabled by default. Rolling mode keeps a bounded history of
owning snapshots; trace mode uses the same bounded data and enables the
intended Chrome Trace workflow.

```cpp
Simulation game {SimulationConfig {
    .Diagnostics = {
        .Mode = DiagnosticsMode::Rolling,
        .HistoryLength = 120,
    },
    .FixedDeltaTime = 0.0,
}};

game.Step({.DeltaTime = 1.0 / 60.0});
const FrameProfile* frame = game.LastFrameProfile();
std::string traceJson = game.ExportChromeTrace();
```

`FrameProfile` owns its schedule, stage, and system names and timings, so it
remains valid after later frames. `Profiles()` exposes the bounded history.
Chrome trace output has schema version 1 and can be loaded through
`chrome://tracing` or Perfetto. Disabled mode retains no history and skips the
timing clock reads.

`FrameInfo::DeltaTime` comes from your application. The simulation fills
`FrameIndex`. Fixed systems may additionally request `FixedStepInfo`, whose
`DeltaTime` is constant and whose `SubstepIndex` starts at zero each frame.

## Time Has Two Jobs

NGIN.ECS keeps gameplay time and data-change time distinct:

- `SimulationTick` advances for every fixed substep and once for the variable
  update.
- `ChangeVersion` advances at each startup, fixed-step, or variable-update
  boundary.
- `FrameIndex` advances once for every attempted `Step`.

After startup, a frame with no fixed work advances the simulation tick once. A
frame with `N` fixed substeps advances it `N + 1` times.

Most code only needs `FrameInfo` or `FixedStepInfo`. The explicit clocks exist
for replay, diagnostics, and [change detection](ChangeDetection.md).

## Resources

Resources are singleton-style values owned by the simulation:

```cpp
struct Gravity { float Y; };

game.InsertResource(Gravity {-9.81f});

game.Schedule(FixedUpdate).System(
    "Apply gravity",
    [](Query<Write<Velocity>>& velocities,
       Resource<const Gravity> gravity,
       const FixedStepInfo& step) {
        velocities.ForEach([&](auto row) {
            row.template Get<Velocity>().Y +=
                gravity->Y * static_cast<float>(step.DeltaTime);
        });
    });
```

Use `Resource<const T>` to read and `Resource<T>` to write. Missing resources
fail with an actionable error instead of silently constructing a value.

## Events

Events are typed messages, not persistent state:

```cpp
struct ShipDestroyed { EntityId Ship; };

update.System("Detect deaths", [](EventWriter<ShipDestroyed> events) {
    events.Send(ShipDestroyed {/* ... */});
});

update.System("Award points", [](EventReader<ShipDestroyed> events) {
    for (const ShipDestroyed& event : events)
    {
        // React to events from the previous change boundary.
    }
});
```

`EventWriter<T>` publishes at the next change boundary. This buffering makes
ordinary producers and consumers easy to schedule in parallel.
`ImmediateEventWriter<T>` publishes in the current boundary and creates a
barrier; use it only when same-boundary visibility is part of the rule.

Code outside systems can queue a next-boundary event with `SendEvent`.

## Custom Schedules

Create a named label, then place it in a custom pipeline:

```cpp
const ScheduleLabel extract = game.CreateSchedule("Extract");
game.Schedule(extract).System("Build render scene", buildRenderScene);

game.SetPipeline({
    {Startup, ScheduleFrequency::Once, true},
    {FixedUpdate, ScheduleFrequency::FixedStep, true},
    {Update, ScheduleFrequency::Frame, true},
    {PostUpdate, ScheduleFrequency::Frame, false},
    {extract, ScheduleFrequency::Frame, false},
});
```

The final flag says whether that entry begins a new change boundary. Use shared
boundaries for several phases of one logical update.

## Failure And Inspection

`Step` does not roll back completed game work. If a later system fails, earlier
changes and already-advanced clocks remain committed. Catch the exception and
inspect `LastStepResult()` to see how far the step got.

`ResetStartup()` asks once-only schedules to run again on the next step.

## Manual Execution

Use the lower-level pieces when another engine owns time:

```cpp
World world;
Schedule schedule;
SerialExecutor executor;

world.AdvanceChangeVersion();
RunContext context {
    .ChangeVersion = world.CurrentChangeVersion()};

RunResult result = schedule.Run(world, executor, context);
```

`Schedule::Run` never advances clocks or change versions. The context version
must match the world. A schedule may rebind to different worlds sequentially,
but the same schedule cannot run concurrently against several worlds.
