# NGIN.ECS 0.4 Performance, Parallelism, And Profiling Plan

Status: Implemented; release gates verified

Implementation status:
[`NGIN-ECS-V0.4-Performance-Parallelism-And-Profiling-Progress.md`](NGIN-ECS-V0.4-Performance-Parallelism-And-Profiling-Progress.md)

Target: `NGIN.ECS 0.4.0`

Depends on:

- [`NGIN-ECS-Simulation-And-Scheduling-Redesign-Plan.md`](NGIN-ECS-Simulation-And-Scheduling-Redesign-Plan.md)
- [`NGIN-ECS-Simulation-And-Scheduling-Redesign-Progress.md`](NGIN-ECS-Simulation-And-Scheduling-Redesign-Progress.md)

## Summary

`NGIN.ECS 0.3.0` established the public model:

```text
World       owns entities and components
Schedule    describes work and ordering
Executor    runs compatible work
Simulation  owns time and coordinates the loop
```

Version `0.4.0` must keep that model recognizable. It is not another
orchestration redesign and it is not a general feature release.

The release has four product goals:

1. structural performance;
2. real multicore execution;
3. dense general-workload performance; and
4. production diagnostics.

Everything in this plan must directly support one of those goals. Correctness,
tests, documentation, portability, and release verification remain mandatory
definition-of-done work, but they do not broaden the feature scope.

The release promise is:

> The NGIN.ECS 0.3 programming model, made fast enough, parallel enough, and
> observable enough for production simulation workloads.

## Why This Release

The archetype storage thesis is already producing strong results. Selective
queries can reject whole populations before entering an entity loop, cached
query plans avoid repeated schema work, and typed chunk spans expose contiguous
data. The current comparison also shows competitive destruction and fragmented
iteration.

The remaining weaknesses are equally clear:

- individual creation performs substantially more work than competing entity
  creation paths;
- direct component lookup is still behind;
- add/remove transitions are disproportionately expensive;
- dense mixed and wide workloads do not win consistently;
- the current executor overlaps independent systems but cannot divide one
  dominant system across workers; and
- last-run diagnostics do not explain sustained frame behavior, worker
  utilization, structural churn, or time lost at barriers.

These are connected problems. Structural and dense-path improvements supply
better kernels for parallel execution. A persistent executor makes those
kernels scalable. Production diagnostics show whether the resulting frame is
actually using the machine effectively.

## Measured Starting Point

### Reference Environment

The initial `0.4` comparison baseline was recorded on 2026-08-01 with:

- Windows 11 Pro;
- Intel Core i7-13700KF;
- Clang;
- `Release`, `-O3`, and `-DNDEBUG`;
- EnTT `3.16.0`;
- 262,144 entities;
- two warmups and the median of fifteen measured samples; and
- matched workload checksums.

The reference is a same-machine optimization baseline, not a universal
performance claim.

| Scenario | NGIN.ECS | EnTT view | Current result |
| --- | ---: | ---: | ---: |
| create with two components | 138.56 ns/op | 30.92 ns/op | 4.48x slower |
| destroy with two components | 23.19 ns/op | 29.62 ns/op | 1.28x faster |
| get two components | 6.01 ns/op | 4.09 ns/op | 1.47x slower |
| remove and re-add velocity | 588.94 ns/op | 8.55 ns/op | 68.9x slower |
| fragmented movement | 1.58 ns/op | 2.54 ns/op | 1.60x faster |
| mixed seven-system update | 2.23 ns/op | 1.95 ns/op | 1.14x slower |
| rare conjunction | 0.85 ns/op | 19.81 ns/op | 23.3x faster |
| exclude a 95% majority | 2.03 ns/op | 33.51 ns/op | 16.5x faster |
| ten overlapping queries | 1.10 ns/op | 3.46 ns/op | 3.15x faster |
| wide-component iteration | 10.96 ns/op | 8.16 ns/op | 1.34x slower |
| destroy wide archetype | 113.50 ns/op | 141.87 ns/op | 1.25x faster |

Specialized EnTT owning groups remain a separate comparison. In the same run,
NGIN.ECS was 1.09x slower for the rare conjunction, 1.30x faster when excluding
the 95% majority, and 2.96x slower for wide iteration.

### Internal Regression Evidence

The Windows Clang Debug regression benchmark over 100,000 entities measured:

- batch spawn approximately 4.03x faster than repeated individual spawn;
- typed chunk writes approximately 1.63x faster than row writes; and
- `Schedule::Each` approximately 11% slower than the direct row-write query.

This evidence identifies leverage points, but `0.4` performance gates must use
optimized Release builds. Debug measurements remain useful for catching large
algorithmic regressions and diagnosing test behavior.

### Benchmark Stability Warning

Earlier seven-sample runs sometimes reported wins for mixed-system and wide
iteration where the longer fifteen-sample run reported losses. Therefore:

- one favorable run is not acceptance evidence;
- relative ratios must be paired with NGIN.ECS absolute baselines;
- p90 and sample spread must remain visible;
- noisy samples must trigger a rerun rather than a baseline rewrite; and
- performance claims must name hardware, compiler, build flags, population,
  warmups, and sample count.

## Scope

### Goal 1: Structural Performance

Structural work includes entity creation, destruction, component addition and
removal, archetype transitions, deferred commands, and the associated entity
location updates.

The `0.4` implementation should:

- add explicit entity and archetype-capacity reservation where measurement
  proves it removes repeated growth;
- improve individual spawn without weakening exception safety;
- retain and extend the fast batch-spawn path;
- batch compatible despawn and transition work;
- reduce repeated component metadata, signature, archetype, and column lookup;
- reuse destination-transition metadata and pre-resolved column mappings;
- reserve command and destination capacity before applying known batches;
- fuse compatible internal command work only where observable recording order
  is preserved;
- avoid redundant mutation/query guards within a validated command-application
  scope; and
- measure narrow, wide, empty-tag, move-only, and throwing component cases.

Structural optimization must preserve:

- generational entity safety;
- complete destination construction before source removal;
- component lifetime and alignment guarantees;
- existing added/changed versions through migration;
- the command completed-prefix guarantee;
- deterministic command order; and
- rollback behavior for partial spawn and destination construction.

The plan does not assume archetype migration can equal sparse-set add/remove.
It does require the current gap to shrink materially.

### Goal 2: Real Multicore Execution

The `0.3` parallel executor can overlap independent systems. That is useful
when a schedule contains enough independent work, but it does not help when one
large movement, animation, or simulation system dominates a frame.

Version `0.4` should add two complementary forms of parallelism:

1. inter-system parallelism from the existing conflict graph; and
2. opt-in intra-system parallelism over independent chunks.

The implementation should:

- replace repeated asynchronous task creation with a persistent worker pool;
- create workers once for the owning executor and reuse them across frames;
- support explicit worker-count configuration and an automatic default;
- retain a caller-thread lane for main-thread and exclusive systems;
- allow eligible per-entity work to partition by stable chunk ranges;
- avoid parallel dispatch below a measured profitability threshold;
- prevent nested oversubscription when a system is already running inside the
  executor;
- give tasks deterministic identities independent of the worker that claims
  them;
- retain stable command/event merge positions where parallel work can emit
  deferred output;
- stop scheduling new work after failure, wait for running work, and report the
  committed boundary; and
- expose queue, worker, barrier, and useful-work measurements to diagnostics.

Intra-system parallelism must be explicitly opted into. Selecting a parallel
executor alone must not silently make an existing `Each` callable execute
concurrently against itself. The precise public spelling is a WS0 decision,
but the semantic distinction is required for source safety and understandable
capture behavior.

Serial execution remains the reference implementation. Parallel and
deterministic-parallel execution must produce the same entity coverage,
component access, change marking, command results, and failure boundaries as
the serial contract. Cross-platform floating-point identity remains out of
scope.

### Goal 3: Dense General-Workload Performance

Selective archetype rejection is already strong. `0.4` must improve ordinary
work where most of the world matches and each row touches several columns.

The implementation should examine and improve:

- `Each` dispatch relative to the direct query backend;
- row iteration over one or several required columns;
- typed chunk-span acquisition;
- wide query column binding and access;
- repeated whole-range change marking;
- cached query-plan validation when the schema is unchanged;
- schedule parameter binding and per-system setup;
- prefetch and loop structure for broad contiguous traversal;
- small-world fixed overhead; and
- mixed frames containing several overlapping systems.

Optimizations must use the same query plan and access contract rather than
creating separate fast and correct APIs. A fast path may specialize dense
physical rows, but it must fall back explicitly when optional or change-filtered
rows are not contiguous.

Existing selective-query advantages are protected assets. Broad and wide
optimization must not flatten the archetype model into per-entity signature
checks or introduce a second storage truth.

### Goal 4: Production Diagnostics

The current schedule exposes compiled stages, constraints, last-run timing,
command counts, failure state, text, JSON, and DOT. `0.4` should extend this
into bounded, low-overhead runtime profiling.

Diagnostics should answer:

- Which systems dominate the frame?
- Which stages ran in parallel and which remained serial?
- How much time was useful work, queue wait, barrier wait, or command apply?
- Were workers busy, idle, or imbalanced?
- Which systems caused structural churn?
- How many entities, archetypes, chunks, and rows did each query visit?
- Did a query plan refresh, and why?
- Which explicit or inferred constraint prevented overlap?
- What committed before a failure?

The implementation should provide:

- disabled, rolling-summary, and trace collection levels;
- bounded history with caller-configured capacity;
- per-system and per-stage minimum, median, p90, maximum, and sample count;
- worker utilization and task-distribution statistics;
- query match/visit counts and query-plan refresh counts;
- command, spawn, despawn, add, remove, set, and transition counts;
- time spent applying deferred work;
- frame/simulation tick/change-version correlation;
- snapshots that remain valid after the next frame;
- a versioned machine-readable schema; and
- Chrome Trace/Perfetto-compatible export for timeline inspection.

Diagnostics must be derived from the same executor and graph state that drives
execution. It must not reconstruct a second, potentially inconsistent model.
Collection must be bounded, thread-safe, and cheap to disable.

## Explicit Non-Goals

The following are deferred from `0.4.0`:

- reusable ECS module or plugin authoring;
- hierarchy and relationship semantics;
- save-game or world serialization;
- stable cross-build component identifiers;
- networking, replication, or rollback simulation;
- physics, rendering, audio, or scene integrations;
- new query-language families such as arbitrary OR/relationship traversal;
- runtime reflection requirements;
- a new storage architecture;
- a new `Simulation`/`Schedule` ownership model;
- compatibility aliases for the removed `Scheduler`; and
- changes whose only purpose is benchmark-score specialization.

Allocator or chunk-size customization is included only if it is directly
required to meet a measured performance goal without exposing storage details.

## Contract Principles

### Preserve The 0.3 Mental Model

Normal programs should continue to create a `Simulation`, register systems on
named schedules, and call `Step`. Existing `Each`, `System`, explicit `Query`,
resources, events, local state, conditions, groups, and manual schedule
execution remain recognizable.

Additive performance controls are acceptable. Renaming the main concepts or
introducing a second application facade is not.

### Optimize Measured Work

Every hot-path change needs:

1. a named benchmark that reproduces the cost;
2. profiling evidence locating the cost;
3. a correctness test protecting the affected contract; and
4. before/after Release measurements from the same environment.

Code complexity is not justified by a statistically unclear improvement.

### Determinism Is A Semantic Mode

Deterministic-parallel mode must retain stable graph choices and stable output
merge order. Stable order must use logical task keys such as schedule, stage,
system, chunk ordinal, and command ordinal—not whichever worker completes
first.

Maximum-throughput mode may expose independent completion order only where the
existing contract permits it.

### No Hidden Access

Parallel safety continues to come from declared component, resource, event,
command, exclusive, and main-thread access. A system may not use globals or
captured references to bypass that declaration.

Opt-in chunk-parallel callables must document that the callable may be invoked
concurrently. Mutable capture is the caller's responsibility unless it is
represented by a supported synchronized parameter.

### Diagnostics Must Pay For Themselves

Disabled diagnostics must not allocate per frame, take global locks in hot
loops, or retain trace records. Rolling summaries should aggregate locally
before publishing. Full tracing is explicitly opt-in.

## Illustrative Public Direction

The following examples describe capability, not frozen spelling. WS0 must
select names and ownership after testing prototypes.

### Reservation

```cpp
world.ReserveEntities(100'000);
world.ReserveArchetype<Position, Velocity, Enemy>(50'000);
```

Reservation must affect capacity only. It must not create entities, change
versions, publish an archetype as populated, or invalidate live handles.

### Explicit Chunk Parallelism

```cpp
SystemId move = update.Each(
    "Move",
    [](Position& position, const Velocity& velocity) {
        position.X += velocity.X;
    });

update.SetIterationPolicy(move, IterationPolicy::ParallelChunks);
```

An alternative dedicated registration function may be selected if it produces
clearer diagnostics and compile-time constraints. Existing `Each` remains
serial within itself unless explicitly opted in.

### Executor Configuration

```cpp
Simulation simulation {
    SimulationConfig {
        .Execution = ExecutionMode::DeterministicParallel,
        .WorkerCount = 8}};
```

Zero may mean automatic hardware-based selection. The final contract must
define whether the caller thread participates and how many background workers
are created.

### Profiling

```cpp
simulation.SetDiagnostics(
    DiagnosticsConfig {
        .Mode = DiagnosticsMode::Rolling,
        .HistoryLength = 240});

const FrameProfile& frame = simulation.LastFrameProfile();
simulation.ExportTrace(output);
```

The final API should make lifetime, allocation, and thread-safety explicit.
Existing schedule text, JSON, and DOT diagnostics should extend or delegate to
the new data rather than disappear.

## Performance Success Criteria

Relative comparisons use the pinned EnTT view unless a row explicitly names an
owning group. Absolute thresholds apply only to the recorded reference machine
and must be re-baselined through reviewed evidence when hardware changes.

| Area | Current reference | `0.4` success target |
| --- | ---: | ---: |
| individual create | 138.56 ns/op, 4.48x slower | at most 75 ns/op and at most 2.5x slower |
| direct two-component get | 6.01 ns/op, 1.47x slower | at most 5.0 ns/op and at most 1.25x slower |
| remove/re-add transition | 588.94 ns/op, 68.9x slower | at most 170 ns/op and at most 20x slower |
| two-component destroy | 23.19 ns/op, 1.28x faster | no more than 5% absolute regression |
| mixed seven-system update | 2.23 ns/op, 1.14x slower | at most 2.0 ns/op and parity or faster than the EnTT view |
| wide iteration | 10.96 ns/op, 1.34x slower | at most 8.2 ns/op and parity or faster than the EnTT view |
| fragmented movement | 1.60x faster | no more than 5% absolute regression |
| rare conjunction | 23.3x faster than view | no more than 5% absolute regression |
| excluded majority | 16.5x faster than view | no more than 5% absolute regression |
| overlapping queries | 3.15x faster | no more than 5% absolute regression |
| `Each` versus row query | about 11% overhead in Debug | at most 10% Release overhead |
| typed chunk versus row write | 1.63x faster in Debug | preserve at least 1.5x advantage |

A target passes only when:

- checksums match;
- the median passes;
- p90 does not show a material regression hidden by the median;
- the result repeats in a second clean run; and
- no correctness, sanitizer, or lifetime guarantee was disabled.

### Parallel Success Criteria

On the reference machine:

- a large memory-oriented chunk-parallel workload should reach at least 2.5x
  speedup with eight workers over the identical serial semantic path;
- a compute-heavy workload should reach at least 4x with eight workers;
- one, two, four, eight, and automatic worker configurations must be recorded;
- workloads below the profitability threshold must remain within 5% of serial;
- repeated frames must not create or destroy worker threads;
- deterministic-parallel command/event output must match across at least 1,000
  repeated stress runs; and
- ThreadSanitizer must report no executor-owned race where supported.

Speedup measurements must include the same change marking, command recording,
merge, and failure semantics in serial and parallel cases.

### Diagnostics Overhead Criteria

- disabled diagnostics: at most 1% median overhead and no per-frame allocation;
- rolling summaries: at most 3% median overhead in the mixed-frame benchmark;
- full trace collection: at most 10% median overhead in its documented target
  workload;
- bounded history: memory use must stay constant after reaching capacity; and
- exporting or formatting diagnostics must not run implicitly in the measured
  simulation hot path.

## Workstreams

### WS0: Contract Freeze And Benchmark Truth

Objective: freeze performance semantics and establish evidence that can detect
real improvement.

Tasks:

1. Record Debug and Release build metadata automatically in benchmark output.
2. Add CPU, worker count, compiler version, build flags, schema version, and
   pinned comparison revision to metadata.
3. Make fifteen measured Release samples the optimization baseline default or
   provide a named release-evidence mode.
4. Report sample spread or another noise indicator alongside min/median/p90.
5. Interleave or otherwise control library execution order where thermal or
   background effects can bias the comparison.
6. Separate NGIN.ECS absolute regression gates from EnTT comparison reports.
7. Add missing batch structural, worker-scaling, scheduling-overhead, and
   diagnostics-overhead scenarios.
8. Freeze reservation semantics.
9. Freeze the explicit intra-system parallel opt-in.
10. Freeze worker-count and caller-thread participation rules.
11. Freeze deterministic task and merge keys.
12. Freeze diagnostics levels, snapshot lifetime, history bounds, and trace
    schema ownership.

Exit criteria:

- the current baseline is reproducible within documented tolerance;
- every `0.4` success metric maps to a named benchmark;
- no public implementation begins with an unresolved semantic question; and
- the benchmark harness can distinguish improvement from machine noise.

### WS1: Entity Lookup, Reservation, And Creation

Objective: reduce fixed world-management cost before optimizing complex
transitions.

Tasks:

1. Profile entity-slot validation, location lookup, component-column lookup,
   signature construction, archetype lookup, allocation, and row publication.
2. Remove redundant lookups without weakening stale-handle validation.
3. Prototype and benchmark entity reservation.
4. Prototype and benchmark archetype/chunk reservation.
5. Pre-resolve complete spawn layouts and component columns.
6. Reuse capacity across batch spawn where lifetime rules permit it.
7. Add varied-value bulk construction only if prototype-based `SpawnBatch`
   cannot express a measured real workload efficiently.
8. Extend exception and rollback tests for reserved and batch paths.

Exit criteria:

- create and direct-get targets are met;
- batch spawn retains its advantage;
- stale, cross-world, throwing, move-only, tag, and over-aligned tests pass; and
- reservation introduces no observable entities or version changes.

### WS2: Transition And Command Throughput

Objective: make structural bursts materially cheaper while preserving ordered
semantics.

Tasks:

1. Profile remove/add migration by metadata, construction, relocation, source
   removal, location repair, and allocation cost.
2. Extend transition-plan caches only where schema invalidation remains clear.
3. Pre-reserve destination chunk capacity for known command batches.
4. Add internal batch paths for compatible despawn/add/remove/set operations.
5. Detect command sequences that can share validation or transition metadata.
6. Do not reorder across commands whose intermediate world state can affect a
   later result or exception.
7. Preserve the completed-prefix guarantee for a failing direct flush.
8. Preserve failed-parallel-stage command discard.
9. Benchmark narrow, wide, tag-only, move-only, and destructor-observable
   components.
10. Add randomized equivalence tests comparing optimized batches with the
    serial command model.

Exit criteria:

- transition target is met;
- command results and exception boundaries match the reference path;
- payload destruction and alignment tests pass; and
- no benchmark-only public operation is introduced.

### WS3: Dense Serial Kernels

Objective: make the best serial implementation competitive before distributing
it across threads.

Tasks:

1. Measure `Each` setup, callable dispatch, plan validation, chunk traversal,
   row access, and change marking separately.
2. Remove repeated plan/schema checks from inner loops.
3. Specialize dense required-column traversal without duplicating query
   semantics.
4. Reduce wide-query binding and per-column access overhead.
5. Coalesce full-range change marking.
6. Evaluate prefetch only with repeatable evidence across narrow and wide rows.
7. Preserve explicit fallback for filtered and non-contiguous logical rows.
8. Record small, medium, and large population behavior.
9. Protect selective and overlapping query results with absolute gates.

Exit criteria:

- mixed and wide targets are met;
- `Each` and typed-chunk targets are met;
- selective workloads remain within their regression limits; and
- row, chunk, and `Each` paths pass common semantic conformance tests.

### WS4: Persistent Executor Runtime

Objective: remove thread-creation churn and provide a measurable execution
substrate for both system and chunk tasks.

Tasks:

1. Implement an executor-owned persistent worker pool using the standard
   library and existing NGIN.Base facilities.
2. Define startup, shutdown, worker failure, and simulation destruction
   behavior.
3. Support explicit and automatic worker counts.
4. Decide and test caller-thread participation.
5. Use bounded task storage and avoid steady-state frame allocations.
6. Schedule inter-system graph nodes on the pool.
7. Preserve isolated caller-thread lanes.
8. Prevent nested executor oversubscription.
9. Implement deterministic logical task IDs independently of queue order.
10. Surface executor measurements without requiring tracing.

Exit criteria:

- repeated frames create no new threads;
- current parallel-system correctness and failure tests pass;
- idle/small-work overhead stays within target; and
- shutdown and exception stress tests leak no threads or tasks.

### WS5: Opt-In Chunk-Parallel Systems

Objective: scale one dominant eligible system across workers.

Tasks:

1. Implement the frozen explicit opt-in for per-entity systems.
2. Partition stable dense work by chunk and bounded grain size.
3. Guarantee each matching entity is visited exactly once.
4. Preserve query filters, optional reads, entity IDs, frame/fixed context, and
   automatic change marking.
5. Define which parameters make a system ineligible for chunk parallelism and
   produce actionable diagnostics.
6. Define deferred-output support only after stable merge keys and failure
   behavior are proven.
7. Evaluate conditions once per system run, not independently per chunk.
8. Stop publishing new tasks after failure and wait for already-running tasks.
9. Compare serial, throughput-parallel, and deterministic-parallel results.
10. Benchmark memory-bound, compute-heavy, small, filtered, wide, and imbalanced
    chunk distributions.

Exit criteria:

- parallel scaling targets are met;
- deterministic stress tests pass;
- ThreadSanitizer is clean where supported;
- serial behavior remains the fallback for ineligible or small work; and
- diagnostics explain why a system did or did not split.

### WS6: Runtime Profiles And Trace Export

Objective: explain frame cost using bounded data captured from the real graph
and executor.

Tasks:

1. Implement disabled, rolling, and trace collection modes.
2. Capture frame, schedule, stage, system, task, barrier, and deferred-apply
   timing.
3. Capture worker occupancy, queue wait, and imbalance.
4. Capture query archetype/chunk/entity visits and plan refreshes.
5. Capture structural operation and transition counts by system.
6. Aggregate rolling min/median/p90/max without unbounded raw history.
7. Define immutable or owning profile snapshots with explicit lifetime.
8. Extend versioned JSON without breaking schema-1 consumers silently.
9. Export Chrome Trace/Perfetto-compatible events outside the hot path.
10. Document interpretation with one oversynchronized and one imbalanced
    example.

Exit criteria:

- diagnostics answer every question listed under Goal 4;
- overhead and memory-bound targets pass;
- exported traces open in a supported timeline viewer;
- snapshots remain valid for their documented lifetime; and
- disabled mode performs no steady-state allocation.

### WS7: Integration, Documentation, And Release

Objective: prove the four goals work together without widening the release.

Tasks:

1. Update `Hello.ECS` only where it can demonstrate parallel policy or
   diagnostics without becoming a benchmark demo.
2. Add a focused performance/parallel example if a compact existing example
   cannot teach the feature clearly.
3. Update the quick start only for new defaults that affect normal users.
4. Add dedicated optimization, parallel-execution, and profiling guides.
5. Document when structural change is appropriate and when stable archetypes
   are the better design.
6. Document benchmark reproduction and interpretation.
7. Verify standalone, installed-package, add-subdirectory, and NGIN package
   consumers.
8. Run supported compiler, OS, sanitizer, deterministic-stress, and performance
   gates.
9. Update package metadata, changelog, migration notes, and progress evidence.

Exit criteria:

- every release gate below passes;
- documentation examples compile in CI;
- no deferred non-goal slipped into the public surface; and
- final benchmark claims are reproducible from checked-in commands.

## Dependency Order

```text
WS0 Contract and benchmark truth
 |
 +--> WS1 Lookup, reservation, creation
 |     |
 |     `--> WS2 Transitions and commands
 |
 +--> WS3 Dense serial kernels
 |     |
 |     `----------------------+
 |                            |
 +--> WS4 Persistent executor |
       |                      |
       +--> WS5 Chunk parallelism
       |                      |
       +--> WS6 Diagnostics <-+
                    |
                    `--> WS7 Integration and release
```

WS1 and WS3 may proceed independently after WS0. WS2 builds on the measured
lookup/reservation primitives from WS1. WS4 can begin after the worker and
failure contracts freeze. WS5 requires the optimized serial kernels and the
persistent executor. WS6 begins with WS4 instrumentation points and integrates
WS2/WS3 counters before release.

## Verification Strategy

### Correctness

- entity lifecycle and generation retirement;
- component construction, relocation, destruction, alignment, and rollback;
- randomized world/model equivalence;
- direct and deferred structural operations;
- command completed-prefix behavior;
- query row/chunk/`Each` conformance;
- change-version behavior for serial and chunk-parallel writes;
- schedule ordering, resources, events, conditions, and local state;
- serial/parallel/deterministic-parallel executor conformance;
- main-thread and exclusive lanes;
- parallel failure and cleanup;
- deterministic task/output stress; and
- diagnostics counter/timing ownership and bounded-history behavior.

### Performance

- individual and batch creation;
- direct lookup;
- narrow and wide destruction;
- individual and batch add/remove transitions;
- command recording, merge, and application;
- row, chunk, and `Each` dense iteration;
- mixed, wide, selective, and overlapping frames;
- serial schedule overhead;
- one/two/four/eight/automatic worker scaling;
- balanced and imbalanced chunk populations;
- executor idle, queue, and barrier overhead; and
- disabled/rolling/trace diagnostics overhead.

### Integration

- Windows, Linux, and macOS where supported;
- Clang, GCC, and MSVC where supported;
- Debug and Release;
- AddressSanitizer and UndefinedBehaviorSanitizer;
- ThreadSanitizer where supported;
- standalone, installed, add-subdirectory, and NGIN package consumers; and
- `Hello.ECS` validate, build, and run.

Verification is required release work. It is not a separate product goal and
does not authorize unrelated feature expansion.

## Risks And Mitigations

### Benchmark Chasing

Risk: code is specialized for the comparison scenarios rather than real
worlds.

Mitigation: pair every comparison with absolute baselines, small/medium/large
populations, randomized shapes, and a frame-shaped mixed workload. Reject
optimizations that do not improve a named user operation.

### Semantic Reordering

Risk: batching structural commands changes exceptions, destructor order,
entity IDs, or intermediate observations.

Mitigation: define legal fusion in WS0, retain a serial reference model, and
run randomized optimized/reference equivalence tests with throwing and
destructor-observable components.

### Parallel Capture Races

Risk: an `Each` callable that was safe when invoked serially contains mutable
captured state and becomes racy when split.

Mitigation: require explicit per-system opt-in, document concurrent invocation,
reject unsupported parameters, and provide ThreadSanitizer examples/tests.

### Memory-Bandwidth Ceiling

Risk: worker count rises while a dense component loop stops scaling.

Mitigation: report scaling curves, bandwidth-oriented and compute-heavy cases,
automatic grain thresholds, and worker utilization rather than promising
linear speedup.

### Executor Complexity

Risk: a custom worker pool introduces shutdown, exception, deadlock, or
oversubscription failures.

Mitigation: keep the executor boundary intact, use bounded ownership, stress
startup/shutdown and failure, prohibit nested pool blocking, and retain the
serial executor as the semantic oracle.

### Profiling Changes The Frame

Risk: tracing locks, allocations, or cache pressure distort the behavior it is
measuring.

Mitigation: make levels explicit, aggregate worker-local data, bound buffers,
measure overhead, and move export/formatting outside execution.

### Public Surface Growth

Risk: reservation, iteration policy, worker tuning, and diagnostics create a
large configuration API.

Mitigation: provide useful defaults, add only controls backed by measured need,
keep storage details private, and freeze names only after prototype consumers
demonstrate a simpler model is insufficient.

### Regression Of Current Strengths

Risk: broad-loop optimization damages selective queries or deterministic
behavior.

Mitigation: protect current strengths with absolute 5% gates and run the full
semantic suite for every alternate traversal path.

## Decisions To Freeze In WS0

1. What is the public reservation API, and does archetype reservation create an
   empty schema entry eagerly?
2. Is chunk-parallel opt-in a registration function, a per-system policy, or a
   typed options argument?
3. Which existing `Each` parameters are eligible for concurrent invocation?
4. Are deferred commands/events supported in the first chunk-parallel slice or
   added only after read/write systems are proven?
5. Does the caller thread participate in ordinary pool work?
6. Does worker count describe total execution threads or background workers?
7. What automatic worker-count and grain-size defaults apply?
8. How are deterministic task and output merge keys encoded?
9. What exact committed-work result is reported when one chunk task fails?
10. What diagnostics levels are public, and which counters exist in each level?
11. Who owns profile snapshots and how long are they valid?
12. Does trace export extend diagnostics JSON or use a distinct versioned
    Chrome Trace schema?
13. What noise threshold invalidates a performance sample set?
14. Which absolute baselines gate commits, and which EnTT ratios gate the
    release?

These decisions must be recorded in the eventual progress document before the
affected public headers are finalized.

## Release Gates

`NGIN.ECS 0.4.0` is ready only when:

- [x] the 0.3 ownership and authoring model remains the documented normal path;
- [x] benchmark metadata and stability rules are automated;
- [x] every performance target maps to a checked-in scenario;
- [x] individual creation meets its target;
- [x] direct component lookup meets its target;
- [x] add/remove transition throughput meets its target;
- [x] command batching preserves serial observable behavior;
- [x] mixed and wide dense workloads meet their targets;
- [x] selective and overlapping queries remain within their regression gates;
- [x] the executor reuses persistent workers across frames;
- [x] chunk-parallel execution is explicit and meets scaling targets;
- [x] serial, parallel, and deterministic-parallel conformance passes;
- [x] deterministic output and failure stress tests pass;
- [x] disabled, rolling, and trace diagnostics meet overhead limits;
- [x] profiles expose system, stage, worker, query, structural, and barrier data;
- [x] trace output opens in a supported Chrome Trace/Perfetto viewer;
- [x] diagnostic memory remains bounded;
- [x] no new required runtime dependency is introduced;
- [x] standalone and supported consumer paths pass;
- [x] supported compiler/OS/sanitizer verification passes;
- [x] documentation examples compile;
- [x] benchmark results and limitations are documented; and
- [x] no explicit non-goal was added opportunistically.

## Definition Of Done

The release is complete when a user can keep their `0.3` simulation structure,
reserve and mutate large populations at materially lower cost, opt a dominant
eligible system into chunk-parallel execution, observe real multicore scaling,
and inspect a bounded profile that explains where the frame went.

The release is not complete merely because one benchmark improves, more
threads execute, or a trace file is produced. All four goals must work together
without weakening lifecycle safety, change detection, deterministic execution,
or the simple normal path.
