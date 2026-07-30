# NGIN.ECS Simulation And Scheduling Redesign Plan

Status: Proposed breaking contract

Target: `NGIN.ECS 0.3.0`

Depends on:

- [`NGIN-ECS-First-Release-Plan.md`](NGIN-ECS-First-Release-Plan.md)
- [`NGIN-ECS-First-Release-Progress.md`](NGIN-ECS-First-Release-Progress.md)
- [`NGIN-ECS-First-Release-Workstreams.md`](NGIN-ECS-First-Release-Workstreams.md)

## Summary

`NGIN.ECS 0.2.x` established a compact and reliable storage, query, command,
change-detection, and serial scheduling core. The next release should preserve
that foundation while replacing the public orchestration model.

The current normal path exposes three concepts without an owner:

```cpp
World world;
Scheduler scheduler;

scheduler.AddSystem(...);
scheduler.Run(world);
```

This is mechanically valid, but it makes the user responsible for binding an
execution plan to its data store and hides a world tick advancement inside
`Scheduler::Run`. The model becomes harder to explain once an application has
startup work, fixed simulation updates, variable updates, post-update work,
multiple schedules, or parallel execution.

The redesign introduces `Simulation` as the normal public entry point:

```text
Simulation
|-- World
|-- Schedule registry and active pipeline
|-- Frame and simulation clock
`-- Executor
```

The central contract is:

```text
World stores state.
Schedule describes work.
Executor performs work.
Simulation advances time and coordinates them.
```

`World` and `Schedule` remain available as focused expert APIs. The normal user
does not have to construct and connect them manually.

## Motivation

### Current Ownership Ambiguity

`World` owns:

- entities and component storage;
- archetypes and chunks;
- component added and changed versions;
- the current world tick;
- active-query mutation guards.

`Scheduler` owns:

- system callables and names;
- component read and write metadata;
- explicit ordering edges;
- conflict-derived stages;
- deferred-command barriers;
- each system's last successful run tick.

`Scheduler::Run(World&)` binds these objects temporarily, advances the world's
tick, creates command storage, constructs queries, runs systems, flushes
commands, and updates per-system baselines.

No object currently represents the complete simulation lifecycle. The caller
implicitly fills that role.

### Time Is Coupled To Schedule Invocation

In `0.2.x`, every successful scheduler run attempt advances the world once.
That rule is understandable for one scheduler and one call per frame, but it is
not a sufficient model for:

- `Startup` followed by `Update`;
- zero or more fixed updates inside one rendered frame;
- `Update` followed by `PostUpdate`;
- editor-only or server-only schedules;
- running an extraction schedule without advancing simulation time;
- manually rerunning one schedule for tests or tools.

A schedule invocation is not inherently a frame or simulation tick.

### The Simple System Path Is Too Verbose

The explicit query API is appropriate for filtered, change-aware, and chunked
work:

```cpp
Query<Write<Position>, Read<Velocity>, Without<Disabled>>& query
```

It is unnecessary ceremony for the common case of applying one function to
each matching entity. C++ reference constness can express the simple read/write
contract:

```cpp
[](Position& position, const Velocity& velocity)
```

The explicit query surface should remain available rather than being replaced.

### `Scheduler` Conflates Three Responsibilities

The current name covers:

- schedule declaration and dependency planning;
- execution policy;
- lifecycle and tick coordination.

These responsibilities must separate before parallel execution is introduced.
A `Schedule` should be an inspectable graph. An `Executor` should determine how
that graph runs. A `Simulation` should decide when it runs and when time
advances.

## Product Direction

`NGIN.ECS 0.3.0` should remain a C++23, archetype-based ECS with:

- strong generational entities;
- structure-of-arrays component storage;
- compile-time typed component access;
- query-declared filters and access;
- deferred structural mutation;
- granular change detection;
- deterministic execution options;
- no required runtime reflection;
- `NGIN::Base` as its only runtime dependency.

The redesign should make the ECS easier to start with while preparing it for:

- multiple named schedules;
- fixed and variable update pipelines;
- resources and injected execution context;
- system-local state;
- events and messages;
- conflict-aware parallel execution;
- execution graph diagnostics and profiling;
- reusable ECS modules.

Bleeding edge does not mean maximizing API novelty. It means making access,
time, ordering, synchronization, and determinism explicit enough that the
implementation can optimize them safely.

## Performance Baseline And Optimization Direction

The archetype storage thesis is working, but the current query path does not
yet extract its full value.

The opt-in `ECSComparisonBenchmarks` target compares NGIN.ECS with EnTT `3.16.0`
in one process using matched populations and checksums. An indicative Windows
Clang 22 Release run at 262,144 entities produced:

| Area | NGIN.ECS relative to an EnTT view | Conclusion |
| --- | ---: | --- |
| Rare four-component conjunction | 7.3 times faster | Strong archetype-level rejection |
| Excluding a 95% majority | 9.9 times faster | Strong archetype-level rejection |
| Ten overlapping queries | 1.27 times faster | Promising general-system behavior |
| Twelve-component destruction | 1.68 times faster | Strong stable-archetype destruction |
| Broad two-component iteration | 2.1 times slower | Query machinery hides storage locality |
| Eight-component iteration | 3.1 times slower | Primary iteration weakness |
| Two-component entity lookup | 2.0 times slower | Direct access needs improvement |
| Two-component creation | 6.0 times slower | Individual and batch creation need improvement |
| Remove and re-add one component | 83 times slower | Archetype migration is too expensive |

These ratios are same-machine evidence, not portable performance promises.
They must be refreshed after material storage, query, compiler, or benchmark
changes.

The benchmark also reports `EnTT.group` separately where EnTT can preconfigure
an owning group for one exact hot query. NGIN.ECS may not beat that specialized
layout in every case. Its intended advantage is dense and predictable access
across arbitrary, selective, and overlapping queries without requiring a
bespoke storage group for each combination.

### Performance Conclusion

NGIN.ECS is already strong where archetype-level selection can discard whole
populations. The most important deficiency is broad dense iteration, which
should be the normal strength of an archetype ECS. This points to query and
access overhead rather than a failed storage model.

The current hot path performs work that should not be present for an
unfiltered dense query:

- `Query::ForEachChunk` builds a physical-row scratch list even when no
  `Added<>` or `Changed<>` filter exists;
- row access maps logical indices through that scratch list;
- `Get<T>()` resolves component columns during iteration;
- mutable `Get<T>()` records change ticks during component access;
- repeated queries rematch archetype signatures instead of consuming a cached
  query plan.

The redesign must not place a parallel executor on top of an unnecessarily
expensive serial inner loop. Query-plan, chunk-access, and change-tracking
costs are part of the `0.3.0` execution design.

### Optimization Order

1. Add a dense query fast path. Queries without row-granular `Added<>` or
   `Changed<>` filters iterate physical rows directly and do not build a row
   scratch list.
2. Resolve and cache component columns once for each matched archetype. The
   per-row path must not search an archetype signature for every `Get<T>()`.
3. Expose typed chunk spans for advanced systems and use the same backend for
   `Schedule::Each`. This is the foundation for auto-vectorization and chunk
   partitioning.
4. Reduce change-tracking cost without weakening its semantics. Dense mutable
   iteration should mark known rows or ranges without repeating type and column
   resolution.
5. Cache matched archetypes, filter columns, optional-column presence, and
   dense-versus-filtered mode against a world schema generation.
6. Add batch spawning that resolves one archetype, reserves entity IDs and
   chunk capacity, and constructs directly into component columns.
7. Cache add/remove transition edges and source-to-destination column mappings,
   then add bulk structural transitions. Structural churn should become
   reasonable, but does not need to equal a sparse-set ECS.
8. Add parallel chunk execution only after the serial chunk path is competitive
   and has explicit access and change-marking semantics.

Frequently changing gameplay state should normally be represented as component
data rather than repeated structural mutation. That guidance does not excuse
avoidable migration overhead: transition lookup, column mapping, allocation,
and bulk movement still require focused optimization.

### Performance Success Criteria

Before `0.3.0`:

- broad two-component iteration is competitive with an EnTT view;
- wide-component iteration is faster than an EnTT view;
- rare-conjunction and majority-exclusion queries retain their archetype
  advantage;
- overlapping-query performance does not regress;
- `Schedule::Each` adds no measurable per-entity overhead over the equivalent
  explicit query;
- typed chunk iteration provides a measurable improvement for wide systems;
- batch creation materially improves on repeated individual `Spawn`;
- structural-transition results and tradeoffs are documented honestly;
- EnTT owning groups remain visible as a separate specialized comparison;
- all comparisons preserve equivalent user-visible work, including NGIN.ECS
  change tracking where that is part of normal mutable access.

## Goals

- Provide one obvious normal owner through `Simulation`.
- Make time advancement an explicit simulation responsibility.
- Support multiple schedules without changing change-detection semantics
  accidentally.
- Rename the execution graph concept from `Scheduler` to `Schedule`.
- Separate schedule description from executor policy.
- Provide a concise per-entity system API with inferred component access.
- Preserve explicit queries for advanced iteration.
- Extend system parameters with frame context, resources, local state, and
  events.
- Compile system access into a dependency graph suitable for serial and
  parallel executors.
- Preserve deterministic execution and command application when requested.
- Expose actionable graph, conflict, barrier, timing, and failure diagnostics.
- Retain standalone CMake and NGIN package consumption without `NGIN.Core`.
- Make the serial dense-query path competitive before using parallelism to
  report aggregate throughput gains.

## Non-Goals

The scheduling redesign does not require:

- rewriting archetype or chunk storage;
- changing entity handle packing;
- adding runtime component types;
- adding relationships, hierarchies, or prefabs;
- adding serialization or replication;
- depending on `NGIN.Core`;
- introducing a stable binary ABI;
- making direct world mutation lock-free;
- supporting concurrent unsynchronized access to one world;
- preserving the `0.2.x` scheduler API through compatibility aliases;
- solving render-graph or job-system concerns outside ECS execution.

Relationships, hierarchy queries, prefab authoring, replication, and reflection
integration should receive separate contracts after the simulation and
execution boundaries are stable.

Avoiding a storage rewrite does not freeze the current query, chunk-access,
spawn, or archetype-transition implementations. Targeted changes required to
make the existing archetype and structure-of-arrays model competitive are in
scope.

## Proposed Architecture

### Simulation

`Simulation` is the normal owning facade. It contains:

- one `World`;
- a registry of named `Schedule` objects;
- an active schedule pipeline;
- frame and simulation clocks;
- executor configuration and executor-owned state;
- resource, event, and system-state stores where those are not world
  components;
- diagnostics and profiling state.

The minimum public shape is:

```cpp
Simulation simulation;

EntityId entity = simulation.Spawn(Position{}, Velocity{});

auto& update = simulation.Schedule(Update);
SystemId move = update.Each(
    "Move",
    [](Position& position, const Velocity& velocity)
    {
        position.X += velocity.X;
    });

simulation.Step(FrameInfo{
    .DeltaTime = 1.0f / 60.0f,
});
```

`Simulation` may forward the most common entity operations, such as `Spawn`,
`Despawn`, `Get`, and `Has`, to reduce ceremony. It must also expose
`GetWorld()` for advanced storage and direct-query work. The forwarding surface
must stay intentionally small rather than duplicating every `World` method.

### World

`World` remains the exclusive owner of entity and component state.

It must not own:

- the application frame loop;
- the active schedule pipeline;
- executor policy;
- wall-clock or fixed-step accumulation;
- system registration;
- system-local state.

`World` continues to enforce query and structural-mutation safety. Its component
change versions remain storage metadata, but advancing those versions normally
belongs to `Simulation`.

Direct/manual users may advance a world change boundary explicitly. Running a
schedule must not do so implicitly.

### Schedule

`Schedule` replaces the public `Scheduler` concept.

It owns:

- systems and stable system IDs;
- system names and callables;
- inferred component and resource access;
- explicit before/after edges;
- system groups and run conditions;
- structural and exclusive barriers;
- the compiled execution graph;
- graph diagnostics.

It does not own:

- a `World`;
- frame or simulation time;
- executor threads;
- the decision to advance a change version;
- the complete application loop.

The low-level execution surface is conceptually:

```cpp
RunResult Schedule::Run(
    World& world,
    Executor& executor,
    const RunContext& context);
```

This is an expert API. `Simulation::Step` is the normal caller.

### Executor

An executor consumes a compiled schedule graph and an execution context.

The initial executor implementations are:

- `SerialExecutor`: registration-stable, deterministic execution;
- `ParallelExecutor`: conflict-aware execution of independent systems;
- `DeterministicParallelExecutor`: parallel execution with deterministic
  command merge and stable observable ordering where the contract requires it.

The exact public configuration may use an enum and internal implementations
instead of exposing an inheritance hierarchy. The architectural boundary must
exist even if the first implementation keeps only a serial backend.

### Pipeline And Schedule Labels

A simulation owns arbitrary named schedules. The library provides conventional
labels without making them mandatory:

- `Startup`;
- `FixedUpdate`;
- `Update`;
- `PostUpdate`.

Applications and modules may declare additional labels.

The active pipeline defines:

- schedule ordering;
- whether a schedule runs once, once per frame, or once per fixed step;
- schedule-level run conditions;
- explicit change-version boundaries.

Schedule labels must be strong IDs with owned diagnostic names. Raw string
lookup may be provided for tools, but execution must not depend on borrowed
string lifetimes.

## Time And Change-Detection Contract

Time concepts must be separated:

- `FrameIndex`: advances once for each outer `Simulation::Step`;
- `SimulationTick`: advances once for each logical simulation update;
- `ChangeVersion`: monotonically identifies component changes;
- `DeltaTime`: variable elapsed time for the current frame;
- `FixedDeltaTime`: configured duration of one fixed step.

The exact integer widths and rollover comparison rules must be documented and
tested.

### Required Rules

- `Schedule::Run` never advances time or change versions implicitly.
- `Simulation::Step` advances `FrameIndex` once.
- Every fixed substep advances `SimulationTick`.
- Variable update policy states explicitly whether it advances
  `SimulationTick` or shares the last fixed-step tick.
- A declared change boundary advances `ChangeVersion` exactly once.
- Running several schedules within one boundary does not create artificial
  changes.
- Each system compares `Added<T>` and `Changed<T>` against its own last
  successful `ChangeVersion`.
- A failed system does not update its successful baseline.
- A failed step reports which clocks and earlier systems were committed.
- Manual schedule execution receives an explicit `RunContext` and never guesses
  whether it represents a frame.

Before implementation, the variable-update tick rule must be fixed with
examples for:

1. a frame with no fixed step;
2. a frame with one fixed step;
3. a frame with several fixed steps;
4. startup followed by update;
5. rerunning one schedule from a tool;
6. a system skipped by a run condition;
7. a system failing after earlier systems succeed.

## System Authoring Model

Two system forms are supported.

### Per-Entity Systems

`Each` is the concise path for ordinary component iteration:

```cpp
SystemId move = update.Each(
    "Move",
    [](Position& position,
       const Velocity& velocity,
       EntityId entity)
    {
        position.X += velocity.X;
    });
```

The signature grammar initially supports:

- `T&` as component write access;
- `const T&` as component read access;
- `EntityId` as the matched entity;
- explicitly wrapped optional component access;
- explicitly declared presence, absence, added, and changed filters;
- supported execution parameters that are unambiguous from component
  parameters.

The design must not infer unsafe or ambiguous intent. Generic lambdas,
overloaded call operators, templated function objects, and ambiguous parameter
types require an explicit component signature or the advanced system form.

The implementation should build on the function-trait and typed-query machinery
already used by the scheduler. It must not create a second query engine.

### Advanced Systems

`System` preserves explicit query and command injection:

```cpp
SystemId move = update.System(
    "Move",
    [](Query<
           Write<Position>,
           Read<Velocity>,
           Without<Disabled>>& query,
       const FrameInfo& frame)
    {
        query.ForEach(
            [&](auto row)
            {
                row.template Get<Position>().X +=
                    row.template Get<Velocity>().X * frame.DeltaTime;
            });
    });
```

This form remains the authority for:

- complex filters;
- chunk iteration;
- multiple queries;
- deferred commands;
- resource access;
- event readers and writers;
- system-local state;
- exclusive world access.

### Ordering

Ordering remains handle based:

```cpp
update.After(move, accelerate);
```

The redesign should also add named system groups:

```cpp
update.AddToGroup(move, SimulationSystems);
update.AfterGroup(RenderPreparation, SimulationSystems);
```

Ordering and grouping must remain inspectable. String names are diagnostic
identities, not the primary relationship key.

## System Parameters

The parameter model should be extensible without granting unrestricted world
access.

Planned parameter families are:

- `Query<Terms...>` for component iteration;
- `Commands&` for deferred structural mutation;
- `Resource<T>` and `Resource<const T>` or an equivalent explicit read/write
  resource form;
- `Local<T>` for state owned by one system instance;
- `EventReader<T>` and `EventWriter<T>`;
- `FrameInfo` and fixed-step context;
- `SystemInfo` for diagnostic identity where needed;
- `ExclusiveWorld` for explicitly isolated unrestricted access.

Every parameter binder must declare:

- component reads and writes;
- resource reads and writes;
- whether it requires main-thread execution;
- whether it introduces a deferred-application barrier;
- whether it requires exclusive world access;
- initialization and teardown requirements;
- behavior when the requested input is unavailable.

Parameters must be acquired from a single run snapshot. A system must not see a
mixture of contexts from different frames or fixed steps.

## Resources, Events, And Local State

### Resources

Resources are typed simulation-level values that are not repeated per entity.
They participate in schedule conflict analysis.

Required behavior:

- one value per resource type unless a keyed-resource contract is added later;
- const access is a read;
- mutable access is a write and conflicts accordingly;
- absence produces an explicit condition result or actionable failure;
- resource lifetime belongs to `Simulation`;
- resources do not masquerade as singleton entities internally unless that
  representation proves beneficial and preserves the public contract.

### Events

Events are buffered communication between systems and schedules.

Required behavior:

- typed event channels;
- explicit reader and writer access;
- defined visibility boundaries;
- deterministic merge order in deterministic execution;
- bounded retention or explicit clearing policy;
- reader cursors owned per system;
- no raw reference retention across buffer rotation.

The contract must distinguish immediate events from next-boundary events.
There must not be one API whose visibility changes accidentally with executor
choice.

### Local State

`Local<T>` belongs to one registered system instance.

Required behavior:

- initialized once when the system is prepared;
- retained across successful and skipped runs;
- destruction when the system or schedule is removed;
- no accidental sharing between systems using the same `T`;
- no concurrent access by more than one invocation of the owning system.

## Execution Graph

The compiled graph must represent:

- systems as stable nodes;
- explicit ordering edges;
- inferred component conflicts;
- inferred resource conflicts;
- structural command barriers;
- event visibility barriers;
- exclusive and main-thread nodes;
- system groups;
- run conditions;
- deferred-application nodes;
- diagnostic reasons for every edge or barrier.

The schedule build must remain deterministic. Equal valid choices use stable
registration order.

### Plan Caching

Schedule metadata that depends only on system declarations may be cached by the
schedule. Query matching data that depends on world archetypes must be cached
against a world schema generation.

Required invalidation inputs include:

- system registration, removal, or ordering changes;
- parameter or condition changes;
- new world archetypes;
- resource registration changes where they affect preparation;
- executor capability changes;
- pipeline changes where they alter barriers.

Rebuilding a plan must never be required manually for correctness.

## Parallel Execution Contract

Parallel execution follows the serial contract; it does not define a separate
semantic mode by accident.

Two systems may run concurrently only when:

- their component access sets do not conflict;
- their resource access sets do not conflict;
- neither requires exclusive or main-thread execution;
- no explicit ordering edge or barrier separates them;
- their event visibility contracts permit it;
- their parameter state is safe for concurrent execution.

### Deferred Commands

Parallel systems must not record into one unsynchronized command buffer.

The executor should provide per-system or per-worker command buffers and merge
them at a declared barrier. Deterministic mode orders command application by:

1. schedule graph order;
2. stable system ID;
3. deterministic query partition;
4. command recording order within that partition.

The implementation must document whether two systems that are unordered in
maximum-throughput mode may observe different structural command order.

### Failure

The parallel failure contract must define:

- whether already-running independent systems finish or are cancelled;
- which successful systems update their baselines;
- whether recorded but unapplied commands are discarded;
- how multiple simultaneous failures are reported;
- whether a world remains valid and which prefix of work is committed.

The first parallel release should prefer a simple, testable failure boundary
over aggressive cancellation.

### Determinism

Determinism has levels:

- deterministic graph construction;
- deterministic system ordering where dependencies permit choices;
- deterministic command merge;
- deterministic entity/chunk partitioning;
- deterministic floating-point results.

The library should guarantee the first three in deterministic mode. It should
not claim cross-platform floating-point determinism without a separate numeric
contract.

## Diagnostics And Tooling

The schedule diagnostics surface should expose:

- schedule and pipeline names;
- system IDs, names, and groups;
- component and resource access;
- explicit and inferred edges;
- edge reasons;
- barriers and flush points;
- executor assignment;
- main-thread and exclusive requirements;
- last successful change version;
- run-condition results;
- execution duration;
- command counts;
- failure location.

Planned output forms:

- an in-process read-only diagnostics API;
- stable human-readable text;
- versioned JSON;
- Graphviz DOT as a development aid.

Visual graph output must use the same compiled graph consumed by execution. It
must not reconstruct a second approximate model.

## Public API Sketch

The following is directional rather than a frozen header contract:

```cpp
#include <NGIN/ECS/ECS.hpp>

using namespace NGIN::ECS;

Simulation simulation{
    SimulationConfig{
        .Execution = ExecutionMode::DeterministicParallel,
        .FixedDeltaTime = 1.0f / 60.0f,
    }};

simulation.InsertResource(GameRules{});

simulation.Spawn(
    Position{},
    Velocity{3.0f},
    Racer{"Hasty Hare"});

auto& update = simulation.Schedule(Update);

const SystemId accelerate = update.Each(
    "Accelerate",
    [](Velocity& velocity, const Acceleration& acceleration)
    {
        velocity.X += acceleration.X;
    });

const SystemId move = update.Each(
    "Move",
    [](Position& position,
       const Velocity& velocity,
       const FrameInfo& frame)
    {
        position.X += velocity.X * frame.DeltaTime;
    });

update.After(move, accelerate);

while (running)
{
    simulation.Step(FrameInfo{
        .DeltaTime = clock.ElapsedSeconds(),
    });
}
```

The final API must preserve NGIN naming conventions and avoid multiple aliases
for the same operation.

## Compatibility And Migration

`0.3.0` is a source-breaking minor release under the package's pre-`1.0`
compatibility policy.

The implementation should not preserve `0.2.x` behavior through permissive
fallbacks or silent compatibility aliases.

Expected migration:

```cpp
// 0.2.x
World world;
Scheduler scheduler;
scheduler.AddSystem("Move", callable);
scheduler.Run(world);
```

```cpp
// 0.3.x
Simulation simulation;
simulation.Schedule(Update).System("Move", callable);
simulation.Step(frame);
```

Advanced/manual migration:

```cpp
World world;
Schedule schedule;
SerialExecutor executor;
schedule.Run(world, executor, context);
```

Migration documentation must explain:

- `Scheduler` to `Schedule`;
- where world tick advancement moved;
- how direct queries select change baselines;
- how `AddSystem` maps to `System` or `Each`;
- how named schedules and pipelines work;
- how deterministic execution compares with `0.2.x`;
- how exception and command-flush behavior changed;
- how to keep a fully manual loop.

Removal of the old API must happen only after the `0.3.0` contract, examples,
tests, and migration guide exist in the same change series.

## Implementation Workstreams

### WS0: Contract And Baseline

Deliverables:

- freeze terminology and ownership decisions;
- record `0.2.x` scheduler, query, command, and change-detection behavior;
- add baseline benchmarks for schedule build and run overhead;
- add multi-schedule timing examples that demonstrate the current ambiguity;
- finalize the frame, simulation-tick, and change-version truth table;
- freeze the low-level manual execution contract.

Acceptance:

- the target semantics can be tested without relying on wall-clock timing;
- every old behavior is classified as preserved, intentionally changed, or
  removed;
- no implementation begins with unresolved time-boundary semantics.

### WS1: Schedule And Time Separation

Deliverables:

- introduce `Schedule`;
- separate graph planning from execution;
- add `RunContext`;
- remove implicit time advancement from low-level schedule execution;
- move successful-run baselines to change-version terminology;
- preserve serial behavior and exception safety;
- add schedule labels and stable IDs.

Acceptance:

- two schedules can run against one world inside one change boundary;
- neither schedule changes the boundary implicitly;
- `Added<T>` and `Changed<T>` observe documented per-system baselines;
- all `0.2.x` serial scheduling tests have mapped `0.3.x` equivalents.

### WS2: Simulation Facade And Pipeline

Deliverables:

- introduce `Simulation` and `SimulationConfig`;
- own `World`, schedules, clocks, and executor configuration;
- provide `Startup`, `FixedUpdate`, `Update`, and `PostUpdate` conventions;
- support custom schedules and pipeline ordering;
- implement fixed-step accumulation;
- add the intentionally small world-operation forwarding surface.

Acceptance:

- a complete example needs only one owning `Simulation`;
- zero, one, and multiple fixed steps per frame follow the time truth table;
- startup runs exactly once unless explicitly reset;
- custom schedules can be inserted without modifying library enums.

### WS3: Per-Entity System Authoring

Deliverables:

- add `Schedule::Each`;
- infer component reads and writes from supported signatures;
- support entity identity and explicit filters;
- produce compile-time diagnostics for ambiguous signatures;
- share query planning and iteration implementation with explicit queries;
- benchmark inferred systems against equivalent explicit queries.

Acceptance:

- the movement example uses `Position&` and `const Velocity&`;
- read/write conflicts inferred from `Each` match explicit-query metadata;
- unsupported generic and overloaded callables fail with actionable guidance;
- the concise path adds no per-entity dynamic dispatch.

### WS3A: Query And Archetype Hot Path

This workstream may begin from the WS0 performance baseline while WS1 proceeds.
Its query-plan ownership and invalidation contract must be integrated with
`Schedule::Each`, the executor boundary, and world schema generations.

Deliverables:

- split dense and row-filtered query iteration paths;
- eliminate row-index scratch construction for unfiltered dense chunks;
- introduce per-archetype query bindings with pre-resolved component, optional,
  and change-tick columns;
- cache matched archetypes against a world schema generation;
- expose typed read and write chunk spans;
- make `Each` and explicit queries consume the same compiled iteration plan;
- define row-, range-, and chunk-level change-marking behavior;
- add batch spawn and cached archetype-transition primitives;
- retain view and owning-group EnTT comparisons for the relevant workloads.

Acceptance:

- unfiltered queries perform no per-row component-column lookup;
- unfiltered queries perform no physical-row scratch allocation or fill;
- query caches update correctly when a new archetype appears;
- typed chunk access cannot bypass declared read/write permissions;
- mutable chunk access preserves the documented change-detection result;
- explicit and inferred systems produce matching results and access metadata;
- broad, wide, selective, overlapping, spawn, lookup, and transition
  benchmarks are recorded with matched checksums;
- the performance success criteria in this plan pass or any exception is
  explicitly accepted with measured evidence.

### WS4: Extended System Parameters

Deliverables:

- add frame and fixed-step context;
- add typed resources;
- add system-local state;
- add event readers and writers;
- extend conflict metadata and barriers;
- define parameter initialization, absence, and teardown.

Acceptance:

- resource conflicts affect graph construction correctly;
- local state is distinct per system instance;
- event visibility is identical under serial and deterministic-parallel
  execution;
- parameters cannot bypass declared world access.

### WS5: Executor Boundary And Serial Reference

Deliverables:

- introduce the executor boundary;
- move serial traversal out of `Schedule`;
- retain a deterministic serial reference implementation;
- define executor-facing graph and command interfaces;
- add executor-independent conformance tests;
- preserve command cleanup and failure guarantees.

Acceptance:

- the serial executor matches the frozen `0.3.x` semantics;
- schedule construction does not depend on a particular executor;
- executor selection does not change change-detection results;
- diagnostics report the selected executor.

### WS6: Parallel Execution

Deliverables:

- add conflict-aware parallel system execution;
- add safe per-system or per-worker deferred command buffers;
- implement deterministic command merge;
- define parallel failure aggregation;
- add main-thread and exclusive-system lanes;
- add race-detection and stress coverage on supported CI platforms.

Acceptance:

- conflicting component and resource systems never overlap;
- independent systems can overlap measurably;
- deterministic mode produces stable command and structural results;
- exception paths leave the world valid;
- ThreadSanitizer or the best supported equivalent passes in CI.

### WS7: Graph Diagnostics And Profiling

Deliverables:

- expose graph nodes, edges, barriers, and reasons;
- add per-system timing and command counts;
- add versioned JSON graph output;
- add optional DOT export;
- report plan rebuild and cache invalidation reasons;
- document diagnostic stability guarantees.

Acceptance:

- every ordering edge can be explained;
- output is generated from the execution graph;
- a user can identify why two systems cannot run concurrently;
- diagnostics do not require mutable access to schedule internals.

### WS8: Documentation, Examples, Packaging, And Release

Deliverables:

- update umbrella and focused headers;
- rewrite quick start, systems, change detection, threading, and API docs;
- add a `0.2.x` to `0.3.x` migration guide;
- update `Hello.ECS` to the one-owner model;
- add fixed-step, resource/event, and parallel examples;
- verify standalone, add-subdirectory, installed, and NGIN package consumers;
- update changelog and compatibility policy;
- execute the supported compiler, sanitizer, and platform matrix.

Acceptance:

- `Hello.ECS` visibly demonstrates `Simulation::Step`;
- the smallest documented example does not manually bind a schedule to a
  world;
- the advanced documentation still demonstrates manual execution;
- all package consumption modes expose the same API;
- no `Scheduler` compatibility layer remains.

## Workstream Dependencies

```text
WS0 Contract and baseline
 |
 +--> WS1 Schedule and time separation
 |     |
 |     +--> WS2 Simulation and pipeline
 |     |
 |     +--> WS4 Extended parameters
 |     |     |
 |     |     +--> WS5 Executor boundary
 |     |
 |     +----------------> WS7 Diagnostics
 |
 +--> WS3A Query and archetype hot path
       |
       +--> WS3 Per-entity authoring
       |
       +--> WS6 Parallel execution <-- WS5

WS2 + WS3 + WS3A + WS4 + WS5 + WS6 + WS7
 |
 `--> WS8 Documentation, packaging, and release
```

WS3A may begin from the frozen WS0 baseline while WS1 proceeds. WS3 requires
both the schedule contract from WS1 and the shared iteration backend from
WS3A. WS2 and WS4 may proceed after WS1. WS4 must define resource and event
conflicts before the parallel executor is finalized. WS6 requires both the
executor boundary and the competitive typed chunk path. WS7 should begin with
the graph model rather than being bolted on after execution.

## Verification Strategy

### Focused Correctness

- frame, simulation-tick, and change-version truth-table tests;
- multi-schedule `Added<T>` and `Changed<T>` tests;
- skipped and failed system baseline tests;
- fixed-step accumulation tests;
- system signature compile-pass and compile-fail tests;
- resource conflict tests;
- event visibility and retention tests;
- local-state lifetime tests;
- graph cycle and edge-reason tests;
- serial and parallel executor conformance tests;
- deterministic command merge tests;
- parallel exception and cleanup tests.

### Storage Regression

The scheduling redesign must rerun existing:

- entity lifecycle tests;
- query access and invalidation tests;
- command alignment and failure tests;
- randomized world/model tests;
- component lifetime and exception-safety tests;
- change-detection rollover tests.

### Integration

- standalone Debug and Release builds;
- supported installed-package consumer;
- add-subdirectory consumer;
- NGIN package wrapper consumer;
- `Hello.ECS` validate, build, and run;
- supported Windows, Linux, and macOS compiler matrix;
- AddressSanitizer and UndefinedBehaviorSanitizer;
- ThreadSanitizer where supported.

### Performance

Record and gate:

- dense unfiltered query iteration;
- row-filtered `Added<>` and `Changed<>` iteration;
- rare-conjunction and majority-exclusion queries;
- overlapping-query frame workloads;
- wide-component row and typed-chunk iteration;
- individual and batch spawn;
- direct entity/component lookup;
- narrow and wide destruction;
- individual and bulk archetype transitions;
- cold and cached schedule build time;
- serial per-system dispatch overhead;
- `Each` versus explicit-query iteration;
- resource and event parameter acquisition;
- command recording and merge;
- one, two, four, and available-core parallel scaling;
- executor idle and barrier overhead;
- schedule and per-system memory overhead.

The comparison target must continue to report ordinary EnTT views and
pre-created owning groups as distinct competitors. The benchmark must keep
setup outside timed regions, validate matched checksums, record entity and
operation counts, and emit minimum, median, and p90 samples. Same-machine
baselines must record compiler, build configuration, platform, and pinned
dependency revisions.

Performance gates must compare identical semantics. Parallel speedup must not be
claimed from benchmarks that remove deterministic merge, change tracking, or
command application performed by the serial case.

## Release Gates

`0.3.0` is ready only when:

- [ ] the time and change-boundary contract is frozen;
- [ ] `Simulation` is the documented normal owner;
- [ ] low-level schedule execution never advances time implicitly;
- [ ] named schedules and a configurable pipeline are implemented;
- [ ] `Each` and explicit `System` authoring produce correct access metadata;
- [ ] resource, local-state, event, and context parameters are documented;
- [ ] serial execution passes the complete correctness suite;
- [ ] parallel execution passes conflict, determinism, and failure tests;
- [ ] graph diagnostics explain all execution constraints;
- [ ] dense queries avoid row scratch construction and per-row column lookup;
- [ ] typed chunk access and cached query plans preserve access and
      change-detection semantics;
- [ ] broad and wide iteration meet the performance success criteria;
- [ ] selective and overlapping queries retain the measured archetype
      advantage;
- [ ] batch spawn and archetype-transition performance are recorded;
- [ ] `Hello.ECS` and maintained package examples use the new API;
- [ ] migration documentation covers every removed `0.2.x` scheduler behavior;
- [ ] no accidental compatibility fallback preserves `Scheduler`;
- [ ] standalone, installed, add-subdirectory, and NGIN consumption pass;
- [ ] supported compiler and sanitizer CI passes;
- [ ] performance and storage regression gates pass.

## Risks And Mitigations

### Facade Growth

Risk: `Simulation` duplicates all of `World` and becomes a large god object.

Mitigation: forward only common lifecycle operations and keep advanced storage
operations behind `GetWorld()`.

### Ambiguous Time Semantics

Risk: fixed steps, variable updates, and manual schedule runs produce
inconsistent change detection.

Mitigation: freeze the truth table in WS0 and make schedule execution incapable
of advancing time.

### Signature Inference Complexity

Risk: C++ callable introspection produces poor diagnostics or accepts ambiguous
parameters.

Mitigation: keep the supported `Each` grammar narrow and require explicit
signatures or `System` for advanced callables.

### Parallel Nondeterminism

Risk: system overlap or command merging changes observable results.

Mitigation: provide a serial reference, deterministic-parallel mode, stable
merge keys, and cross-executor conformance tests.

### Hidden Synchronization Cost

Risk: a graph appears parallel but spends most time at command, event, or
resource barriers.

Mitigation: expose barrier reasons and durations through the same graph used by
execution.

### Benchmark Overfitting

Risk: NGIN.ECS is tuned for selective synthetic cases while broad iteration,
small worlds, structural workloads, or real systems regress.

Mitigation: retain a workload matrix covering broad, selective, overlapping,
wide, lookup, creation, destruction, and transition behavior. Compare EnTT
views and owning groups separately, preserve matched checksums, and treat
same-machine ratios as evidence rather than universal claims.

### Premature Feature Coupling

Risk: relationships, reflection, rendering, or `NGIN.Core` lifecycle concerns
distort the ECS execution contract.

Mitigation: keep them outside `0.3.0` unless a minimal integration point is
required and independently justified.

## Decisions To Freeze Before Implementation

The following questions must be answered in WS0:

1. Does variable `Update` advance `SimulationTick`, or only fixed steps?
2. Which pipeline boundaries advance `ChangeVersion`?
3. Are default schedule labels global constants, typed labels, or registered
   strong IDs?
4. Does `Simulation::Step` accept elapsed time only, or a complete
   caller-authored `FrameInfo`?
5. Which optional-component spelling is unambiguous in `Each` signatures?
6. Are missing resources a skipped-system condition, a returned error, or a
   precondition failure?
7. When do events become visible to readers in the same schedule?
8. What observable ordering does maximum-throughput parallel mode guarantee?
9. What is the committed-work boundary when one parallel system fails?
10. Can one compiled `Schedule` run against multiple compatible worlds, and
    where is per-world query cache state stored?
11. Does typed mutable chunk access mark every yielded row changed, require an
    explicit written range, or expose both modes?
12. Which world schema-generation changes invalidate query bindings, and can
    new archetypes update them incrementally?

These are contract decisions, not implementation details. They must be
resolved before the corresponding public headers are committed.

## External Design References

The redesign may learn from, but does not copy or promise compatibility with:

- Bevy ECS schedules, system parameters, resources, deferred commands, and
  executor separation;
- Flecs world-level `progress`, pipelines, access-aware synchronization, and
  graph-oriented ECS model;
- EnTT organizer-style dependency graph construction.

External designs are evidence for tradeoffs, not substitutes for an explicit
NGIN.ECS contract.
