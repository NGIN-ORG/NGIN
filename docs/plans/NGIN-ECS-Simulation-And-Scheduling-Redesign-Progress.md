# NGIN.ECS Simulation And Scheduling Redesign Progress

Updated: 2026-08-01

Target: `NGIN.ECS 0.3.0`

## Frozen Contract

- Variable `Update` advances `SimulationTick` once every frame. Fixed substeps
  each advance it once before the variable update.
- Startup, every fixed substep, and variable update begin change boundaries.
  `Update` and `PostUpdate` share the variable boundary.
- Default schedules use strong `ScheduleLabel` constants. Custom labels are
  registered with simulation-owned names.
- `Step` accepts `FrameInfo`; callers provide `DeltaTime`, while simulation owns
  `FrameIndex`.
- Optional `Each` reads use `OptionalComponent<T>`.
- Missing resources fail with an actionable exception.
- `EventWriter<T>` is next-boundary; `ImmediateEventWriter<T>` is same-boundary
  and introduces a barrier.
- Maximum-throughput parallel mode does not promise observable order between
  independent systems. Deterministic mode merges commands in stage/system order.
- Parallel failure waits for running peers, commits successful direct writes and
  baselines, and discards all unflushed commands in the failed stage.
- A schedule can rebind sequentially across worlds. Query-plan state is owned by
  each registered system and invalidated by world identity/schema generation.
- Mutable typed chunk spans mark every yielded row changed.
- New archetypes and `Clear` advance `SchemaGeneration` and invalidate cached
  bindings.

The full public time truth table is documented in
`Dependencies/NGIN/NGIN.ECS/docs/Simulation.md`.

## Workstreams

- [x] WS0: terminology, ownership, time truth table, failure boundary, and
  manual execution contract frozen.
- [x] WS1: `Schedule`, explicit `RunContext`, last-successful change versions,
  strong IDs, and no implicit world advancement.
- [x] WS2: `Simulation`, default/custom schedules, configurable pipeline,
  startup/fixed/variable execution, and small world forwarding surface.
- [x] WS3: inferred `Each`, entity/context/optional parameters, explicit
  filters, shared query backend, and ambiguous-callable diagnostics.
- [x] WS3A: dense physical-row path, pre-resolved columns, schema-aware query
  plans, typed spans, range change marking, batch spawn, and transition caches.
- [x] WS4: frame/fixed context, resources, per-system local state, buffered and
  immediate events, and conflict metadata.
- [x] WS5: executor interface and deterministic serial reference.
- [x] WS6: conflict-stage parallel execution, isolated command buffers,
  deterministic merge, failure aggregation, and caller-thread lanes.
- [x] WS7: compiled constraint reasons, access/stage inspection, conditions,
  timing, command/failure state, text, versioned JSON, and DOT.
- [x] WS8 implementation: umbrella/focused headers, 0.3 documentation and
  migration guide, `Hello.ECS`, examples, package version, and focused tests.

## Verification Status

- [x] Schedule, simulation, query-cache/span, batch-spawn, lifecycle, and SoA
  tests build and pass in the existing Windows Clang Debug tree.
- [x] Compile-fail coverage rejects ambiguous/generic `Each` callables.
- [x] Regression benchmark records minimum/median/p90 and includes batch spawn,
  typed chunk iteration, and `Each` dispatch.
- [x] Release EnTT comparison completed with matched checksums. At 262,144
  entities, NGIN.ECS was faster for fragmented movement, mixed-system update,
  wide iteration, selective queries, overlapping queries, and wide destruction;
  lookup, individual creation, and especially structural transitions remain
  documented tradeoffs.
- [x] Installed-package and add-subdirectory consumers pass locally.
- [ ] Run the supported OS/compiler and sanitizer matrix in CI.

The unchecked items are release-environment gates, not missing public API
fallbacks. No `Scheduler` header or compatibility alias remains.
