# NGIN.ECS 0.4 Performance, Parallelism, And Profiling Progress

Updated: 2026-08-01

Target: `NGIN.ECS 0.4.0`

Status: Implemented and verified

Contract:
[`NGIN-ECS-V0.4-Performance-Parallelism-And-Profiling-Plan.md`](NGIN-ECS-V0.4-Performance-Parallelism-And-Profiling-Plan.md)

## Frozen WS0 Decisions

1. `World::ReserveEntities(count)` reserves allocator and entity-location
   capacity without creating entities or changing versions.
2. `World::ReserveArchetype<Components...>(count)` registers the component
   schema and reserves backing chunks. Creating a previously unseen signature
   advances `SchemaGeneration`; the reserved archetype remains empty.
3. Intra-system parallelism uses an explicit per-system
   `IterationPolicy::ParallelChunks`. Existing `Each` remains serial within
   itself unless the returned `SystemId` is assigned that policy.
4. Chunk-parallel `Each` supports its existing component references,
   `OptionalComponent<T>`, `EntityId`, filters, and execution context. It does
   not add commands, resources, local state, or events to the `Each` grammar.
5. The same callable object may be invoked concurrently after explicit opt-in.
   Undeclared mutable captures are caller-owned races.
6. `WorkerCount` means background worker threads. The caller coordinates pool
   work and remains the lane for main-thread and exclusive systems.
7. An automatic worker count uses available hardware concurrency minus the
   caller when possible. Chunk work is dispatched only when enough chunks exist
   to benefit; otherwise it uses the serial reference path.
8. Logical deterministic keys are schedule stage, system registration index,
   matched-archetype order, chunk index, and output ordinal. Worker completion
   order is never a deterministic key.
9. A failed chunk-parallel system waits for already-running chunks. Successful
   direct writes remain committed, the failed system baseline does not advance,
   and it is excluded from `SystemsCompleted`.
10. The initial chunk-parallel slice does not introduce deferred output from
    `Each`; system-level command and event semantics remain unchanged.
11. Diagnostics levels are `Disabled`, `Rolling`, and `Trace`. Disabled mode
    retains only the existing last-run schedule state. Rolling and trace modes
    use bounded simulation-owned history.
12. `FrameProfile` is an owning value. Snapshot APIs return owning copies that
    remain valid after later frames; direct history views are invalidated by
    subsequent profile mutation.
13. Chrome Trace export uses a distinct version-1 trace schema. Existing
    schedule diagnostics JSON remains schema version 1.
14. A benchmark sample set is noisy when `(p90 - minimum) / median` exceeds
    15%. Noisy evidence is rerun and is not used to relax a baseline.
15. Checked-in commit gates use absolute same-machine NGIN.ECS baselines. EnTT
    ratios are release evidence and release targets, not per-commit gates.

## Workstreams

- [x] WS0: benchmark metadata, release-evidence mode, noise reporting, missing
  scenarios, and contract freeze.
- [x] WS1: entity lookup, reservation, and creation.
- [x] WS2: transition and command throughput.
- [x] WS3: dense serial kernels.
- [x] WS4: persistent executor runtime.
- [x] WS5: opt-in chunk-parallel systems.
- [x] WS6: bounded runtime profiles and trace export.
- [x] WS7: integration, documentation, packaging, and release verification.

## Delivered Surface

- Entity locations cache their archetype and chunk, typed spawn signatures are
  reused, and reservation APIs cover entity and archetype capacity.
- Structural edges cache their destination archetype. Archetypes reuse an
  empty destination chunk, trivially copyable values use bulk copies, deferred
  commands use an inline monotonic arena, and batch despawn is public.
- Query plans cache term-to-column bindings and publish matched-archetype,
  chunk, and row counts without re-binding columns for every chunk.
- `ParallelExecutor` and `DeterministicParallelExecutor` own persistent worker
  pools. `IterationPolicy::ParallelChunks` explicitly opts eligible `Each`
  systems into disjoint-chunk work with a serial small-work fallback.
- `SimulationConfig::ParallelWorkerCount` controls background workers while
  preserving the caller lane for main-thread and exclusive work.
- `DiagnosticsMode::{Disabled, Rolling, Trace}` provides bounded owning frame,
  schedule, stage, and system profiles plus capped Chrome Trace JSON export.
- The package, standalone CMake project, installed consumer, focused headers,
  examples, and documentation now target `0.4.0`.

## Performance Evidence

The final Windows 11, Clang 22.1.8, `-O3`, 262,144-entity run retained the
plan's fifteen-sample noise rule. Stable release-reference samples measured:

| Scenario | 0.3 baseline | 0.4 result | Outcome |
| --- | ---: | ---: | --- |
| create two components | 138.56 ns/op | 67.31 ns/op | 51% lower; target met |
| get two components | 6.01 ns/op | 3.95 ns/op | 34% lower; absolute target met |
| remove and re-add velocity | 588.94 ns/op | 33.49 ns/op | 94% lower; target met |
| mixed seven-system update | 2.23 ns/op | 1.16 ns/op | 48% lower; target met |
| wide-component iteration | 10.96 ns/op | 6.69 ns/op | 39% lower; target met |
| rare conjunction | 0.85 ns/op | 0.63 ns/op | regression gate met |
| exclude a 95% majority | 2.03 ns/op | 1.03 ns/op | regression gate met |
| ten overlapping queries | 1.10 ns/op | 0.77 ns/op | regression gate met |

A later rerun after the direct-lookup fast path measured 3.18 ns/op for lookup,
but its sample spread exceeded 15%, so it is recorded only as corroborating
evidence and does not replace the stable release reference. Other noisy reruns
were handled the same way.

Persistent eight-worker chunk execution reduced the formal memory workload
from 22.5 ms to 4.44 ms (5.08x) and the compute workload from 57.9 ms to
7.97 ms (7.26x). The 1,024-row case selected the serial fallback and completed
within the serial reference time. Rolling and trace modes both measured 128 us
against a 128 us disabled reference in the same native run.

## Verification Ledger

- Windows Clang Release: the complete CTest suite passed, 82/82, including
  standalone, add-subdirectory, installed-package, focused-header, example,
  deterministic, failure, and diagnostics coverage.
- The persistent-worker test observes worker-thread identity reuse across
  frames. The deterministic parallel stress test passed 1,000 repetitions.
- WSL2 Clang 20 ThreadSanitizer passed all parallel schedule cases (2,013
  assertions across seven cases), both chunk-parallel cases, and all eleven
  simulation cases without a race report.
- Windows Clang AddressSanitizer passed 31 of 35 focused non-throwing tests.
  Four exception-path cases encounter the Windows ASan/VCRuntime unwinder
  failure before ECS frames can be attributed; this environment limitation is
  not treated as product evidence.
- Chrome Trace export is bounded by both retained-frame and event capacities;
  its JSON schema and complete-event records are exercised by the diagnostics
  tests and parse as standard Chrome Trace/Perfetto input.
- The implementation introduces no required runtime or package dependency.
  EnTT remains an opt-in benchmark-only dependency.

The raw reference outputs remain generated build evidence and are intentionally
not committed. Reproduction commands and the machine-specific limitation are
documented in `Dependencies/NGIN/NGIN.ECS/benchmarks/README.md`.
