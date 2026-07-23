# NGIN.ECS First Release Workstreams

Status: Proposed execution plan

Target: `NGIN.ECS 0.2.0`

This document turns
[`NGIN-ECS-First-Release-Plan.md`](NGIN-ECS-First-Release-Plan.md) into ordered
implementation work. The release contract in that document wins if this file
becomes inconsistent with it.

Progress is tracked in
[`NGIN-ECS-First-Release-Progress.md`](NGIN-ECS-First-Release-Progress.md).

## Execution Rules

- Keep the existing archetype/SoA design unless a correctness test proves that
  a local refactor cannot make it safe.
- Break prototype APIs directly; do not add compatibility aliases before the
  first supported release.
- Land behavioral changes with focused tests and documentation in the same
  change.
- Keep storage detail out of normal public headers.
- Establish a performance baseline before changing hot loops.
- Prefer compile-time access enforcement to runtime checks where query terms
  contain enough information.
- Use `NGIN.Base` containers, memory primitives, assertions, and function
  wrappers where they meet the requirement; do not add a third-party runtime
  dependency.
- Run targeted tests while implementing a workstream and the full release
  matrix only at release-candidate gates.

## Dependency Order

```text
WS0 Baseline and contract freeze
  |
  +--> WS1 Storage and lifetime correctness
  |      |
  |      +--> WS2 Entity and component contract
  |               |
  |               +--> WS3 Query and change detection
  |                         |
  |                         +--> WS4 Commands
  |                                  |
  |                                  +--> WS5 Scheduler
  |
  +--> WS6 Build and packaging
  |
  +--> WS7 Documentation and examples
              |
              +--> WS8 CI, performance, and release
```

`WS6` can proceed in parallel after the baseline contract is fixed. `WS7`
starts early but finalizes only after the public API work is complete.

## Priority Classes

- **P0**: memory safety, object lifetime, or world corruption.
- **P1**: public contract can silently produce incorrect behavior or fail a
  supported consumption path.
- **P2**: material usability, diagnostics, documentation, or performance issue.
- **P3**: post-release improvement that does not block safe use.

No P0 or P1 item may be deferred from `0.2.0`.

## WS0: Baseline And Contract Freeze

Objective: make the current state measurable and prevent design drift during
the overhaul.

Primary areas:

- `Dependencies/NGIN/NGIN.ECS/tests/`
- `Dependencies/NGIN/NGIN.ECS/benchmarks/`
- `Dependencies/NGIN/NGIN.ECS/docs/`
- the three first-release planning documents

Tasks:

1. Configure a standalone ECS test build using the existing test preset.
2. Record the current test inventory and make sure every test is discoverable
   through CTest.
3. Run the current focused suite in Debug and Release.
4. Run ASan/UBSan on the current implementation and record known failures.
5. Replace the one-shot benchmark driver with a repeated harness or capture a
   temporary pre-refactor baseline before modifying hot code.
6. Record baseline timings and memory-per-entity for the release benchmark
   scenarios.
7. Add compile-only consumer fixtures for umbrella-header and focused-header
   usage.
8. Record review acceptance of the target API contract before implementing
   public names.

Exit criteria:

- baseline tests and benchmark results are recorded in the progress file;
- known sanitizer failures are listed as P0 items;
- the proposed public API contract has been accepted for implementation.

## WS1: Storage And Lifetime Correctness

Objective: establish memory and exception safety before changing user-facing
access.

Primary files:

- `include/NGIN/ECS/Archetype.hpp`
- `include/NGIN/ECS/World.hpp`
- new internal storage headers as needed
- focused archetype, lifecycle, and world tests

### WS1.1: Chunk Ownership

Priority: P0

Tasks:

- replace constructor-time raw allocation accumulation with RAII-owned columns
  or an explicit rollback guard;
- verify every component, added-tick, and changed-tick allocation is released
  when any later allocation fails;
- centralize size/alignment calculation and deallocation metadata;
- cover over-aligned and larger-than-default-row component storage;
- make zero-capacity and arithmetic-overflow cases explicit;
- keep empty tags allocation-free.

Tests:

- injected allocator failure at each allocation point;
- alignment checks for `alignas(64)` and a platform-supported larger alignment;
- destruction/allocation counters after failed and successful chunk creation;
- a component whose row size exceeds the nominal chunk byte target.

### WS1.2: Row Construction And Removal

Priority: P0

Tasks:

- preserve destination rollback for partially constructed rows;
- make swap removal safe when relocation can throw;
- update relocated entity locations only after the destination row is valid;
- verify chunk compaction updates every moved entity location;
- preserve tick columns during row moves and migrations;
- remove empty chunks without invalidating unrelated chunk locations.

Tests:

- throw from each component construction position;
- despawn first, middle, and last rows;
- empty-chunk removal when it is and is not the last chunk;
- repeated migration followed by despawn;
- copy-only, move-only, and throwing-move components.

### WS1.3: World Transitions

Priority: P0

Tasks:

- make spawn publish an entity as live only after its row is valid;
- make add/remove migration commit only after destination construction;
- make replacement avoid destroying the old value before a viable replacement
  exists;
- define the guarantee for move-only throwing components and reject unsupported
  transitions at compile time;
- keep entity slots and archetype rows synchronized on every failure path;
- make clear destroy every live object exactly once.

Tests:

- failed spawn does not leak an entity or row;
- failed add leaves the original entity valid;
- failed set leaves a valid original or replacement according to the documented
  guarantee;
- failed migration does not leave two rows for one entity;
- randomized operation sequences preserve a reference model.

Exit criteria:

- all storage invariants in the master plan are covered;
- ASan/UBSan report no storage or lifecycle failure;
- no supported component operation can leave a destroyed live slot.

## WS2: Entity And Component Contract

Objective: establish strong identities and compile-time component rules.

Primary files:

- `include/NGIN/ECS/Entity.hpp`
- `src/Entity.cpp`
- `include/NGIN/ECS/TypeRegistry.hpp`
- entity and type-registry tests

### WS2.1: Strong EntityId

Priority: P1

Tasks:

- replace the integer alias with a strong, trivially copyable value type;
- provide null, equality, ordering, hashing, and diagnostic accessors;
- prevent implicit integer mixing;
- keep the packed layout private;
- track free/live slot state explicitly rather than inferring all state from a
  generation value;
- retire a slot instead of allowing generation resurrection;
- document that packed entity handles do not encode world ownership;
- preserve cheap pass-by-value behavior.

Tests:

- type traits and compile-time conversion rejection;
- null, create, destroy, recycle, clear, and stale behavior;
- fabricated current-generation free handles are not live;
- generation exhaustion retires a test-configured small-generation slot;
- cross-world scope is covered by contract tests and documentation without
  promising runtime detection.

### WS2.2: Component Metadata

Priority: P1

Tasks:

- normalize component cv/ref types;
- reject non-object, non-destructible, and non-relocatable types clearly;
- add diagnostic type names to metadata;
- detect incompatible `TypeId` collisions;
- audit bitwise relocation so it does not imply copyability;
- add a compile-time unique-component utility reused by spawn and query;
- document that type IDs are not persistence IDs.

Tests:

- trivial, non-trivial, empty, copy-only, move-only, and custom-relocatable
  metadata;
- collision detection through an injectable test ID policy;
- duplicate spawn components fail to compile;
- invalid component diagnostics are exercised in compile-fail tests.

Exit criteria:

- entity and component contracts are represented in types, not documentation
  alone;
- all old integer helper usage is migrated;
- storage consumes the revised metadata without unsafe fallbacks.

## WS3: Queries And Change Detection

Objective: make declared access authoritative and make change detection
reliable by default.

Primary files:

- `include/NGIN/ECS/Query.hpp`
- `include/NGIN/ECS/World.hpp`
- query and change-detection tests
- query, entity, and change-detection documentation

### WS3.1: Term Validation

Priority: P1

Tasks:

- implement the final `Read`, `Write`, `Optional`, `With`, `Without`, `Added`,
  and `Changed` terms;
- separate access terms from filter terms;
- reject duplicate and contradictory terms at compile time;
- make `Added` and `Changed` grant no data access;
- remove lowercase aliases and prototype `Opt` naming.

Tests:

- positive metadata checks for each term;
- compile-fail cases for duplicate, read/write, optional/required, and
  with/without conflicts;
- filter-only queries can inspect entity identity but not component data.

### WS3.2: Typed Views

Priority: P1

Tasks:

- replace generic mutable `RowView` and `ChunkView` with query-typed views;
- constrain `Get<T>` and `TryGet<T>` from terms at compile time;
- return const access for reads and mutable access for writes;
- remove public escape hatches to raw archetype/component pointers;
- define row, chunk, pointer, and reference lifetimes;
- prevent or discourage retaining views beyond callbacks;
- retain a chunk path named `ForEachChunk`.

Tests:

- read, write, optional, tag-filter, and change-filter iteration;
- undeclared access and write-through-read fail to compile;
- row entity IDs match direct world access;
- chunk and row iteration produce identical entity sets;
- references remain stable for non-structural writes during the callback.

### WS3.3: Structural Guard

Priority: P0

Tasks:

- add an exception-safe active-iteration guard to `World`;
- reject spawn, despawn, add, remove, clear, and other structural operations
  during iteration;
- allow non-structural reads and writes that match the query contract;
- define nested read-only iteration behavior;
- ensure guard depth is restored when a callback throws.

Tests:

- each structural operation is rejected inside row and chunk callbacks;
- commands may be recorded during iteration;
- nested read-only queries work;
- throwing callbacks leave the world usable and guard depth at zero.

### WS3.4: Automatic Change Tracking

Priority: P1

Tasks:

- mark changed when mutable world or query access is acquired;
- preserve explicit `MarkChanged` for interior mutation;
- normalize epoch names to tick names;
- use rollover-safe tick comparison;
- preserve ticks through migration and compaction;
- ensure direct and scheduled query baselines follow the release contract.

Tests:

- no manual mark is needed after normal mutable access;
- read access does not mark changed;
- changed filtering is row granular;
- added and changed can be combined with declared access;
- direct explicit baselines and per-system baselines behave correctly;
- rollover behavior is tested through a test-only small tick type or injected
  tick state.

Exit criteria:

- a query cannot mutate data outside its declared write set;
- structural invalidation cannot occur silently;
- normal writes are visible to changed queries without manual bookkeeping.

## WS4: Deferred Commands

Objective: make deferred structural changes safe and practical.

Primary files:

- `include/NGIN/ECS/Commands.hpp`
- possible `src/Commands.cpp` and detail headers
- command tests

### WS4.1: Operation Storage

Priority: P0

Tasks:

- replace the byte vector with aligned, lifetime-aware operation storage;
- use NGIN allocator facilities and avoid a new runtime dependency;
- guarantee that growth does not relocate live operations incorrectly;
- preserve move-only payload support;
- keep `Commands` non-copyable and non-movable for `0.2.0`;
- make clear and destruction noexcept and exactly-once.

Tests:

- many operations force every growth path;
- over-aligned operation payloads;
- move-only values with lifetime counters;
- clearing and destroying an unflushed buffer;
- repeated record/clear cycles under ASan/UBSan.

### WS4.2: Flush Semantics

Priority: P0

Tasks:

- implement completed-prefix semantics;
- clear all operation records and payloads before an apply exception escapes;
- prevent double destruction of completed operations;
- document world state after a partial flush;
- make reentrant flush and recording-during-flush invalid;
- define stale-target behavior consistently.

Tests:

- first, middle, and last operation throws;
- completed operations remain visible;
- failing and pending payloads are destroyed once;
- the buffer is empty and reusable after failure;
- reentrant misuse is diagnosed.

### WS4.3: Deferred Entities

Priority: P2

Tasks:

- add a strong buffer-local `DeferredEntity` token;
- let spawn return a token;
- let later add, remove, set, and despawn commands target live or deferred
  entities through a constrained target type;
- reject tokens from another command buffer or generation;
- invalidate tokens on clear, failed flush, and completed flush;
- remove `ClearWorld`; clearing is immediate or exclusive world access.

Tests:

- spawn then add/set/remove in one buffer;
- multiple deferred entities resolve deterministically;
- cross-buffer and expired tokens fail;
- a failed spawn invalidates dependent operations predictably.

Exit criteria:

- command recording and flush are free of alignment and lifetime UB;
- failure behavior is deterministic and documented;
- users can compose operations around a deferred spawn.

## WS5: Systems And Scheduler

Objective: provide a deterministic, self-validating serial scheduler.

Primary files:

- `include/NGIN/ECS/Scheduler.hpp`
- possible `src/Scheduler.cpp` and detail headers
- scheduler tests and systems documentation

### WS5.1: Registration Surface

Priority: P2

Tasks:

- add `Scheduler::AddSystem`;
- return a strong `SystemId`;
- own system names;
- own callables through C++23 `std::move_only_function`;
- retain an explicit exclusive-system path;
- make unsupported parameter types a clear compile-time error;
- remove the two-step `MakeSystem`/`Register` golden path.

Tests:

- temporary names remain valid;
- move-only captures work;
- duplicate names and invalid parameters are diagnosed;
- query, commands, and exclusive parameters derive correct metadata.

### WS5.2: Planning And Ordering

Priority: P1

Tasks:

- mark the plan dirty on registration or ordering mutation;
- auto-build on run;
- keep eager `Build` for early validation;
- add `Before`/`After` constraints using `SystemId`;
- detect ordering cycles and report the involved system names;
- preserve deterministic registration order for otherwise unordered systems;
- model commands as structural barriers;
- keep unrestricted world systems exclusive.

Tests:

- registration after a previous run is included automatically;
- read/read systems share a stage;
- read/write and write/write systems conflict;
- explicit ordering affects otherwise independent systems;
- cycle diagnostics name the cycle;
- structural and exclusive barriers isolate stages.

### WS5.3: Run Semantics And Diagnostics

Priority: P1

Tasks:

- advance the world tick once per run;
- update a system's last-run tick only after it succeeds;
- clean pending commands when a system throws;
- define whether later systems run after failure; for `0.2.0`, stop and
  propagate the failure;
- replace raw stage vectors with read-only diagnostics;
- expose names, IDs, stages, access sets, barriers, and ordering reasons for
  tests and tooling.

Tests:

- repeated successful runs;
- system failure followed by a later successful run;
- command cleanup on failure;
- diagnostics match the executed order;
- change queries use each system's last successful tick.

Exit criteria:

- users cannot run a stale schedule;
- execution and failure order are deterministic;
- scheduler metadata accurately bounds all permitted world access.

## WS6: Build, Install, And Package Integration

Objective: make all supported consumption paths equivalent.

Primary files:

- `Dependencies/NGIN/NGIN.ECS/CMakeLists.txt`
- `Dependencies/NGIN/NGIN.ECS/cmake/NGINECSConfig.cmake.in`
- `Dependencies/NGIN/NGIN.ECS/CMakePresets.json`
- `Packages/NGIN.ECS/CMakeLists.txt`
- `Packages/NGIN.ECS/NGIN.ECS.nginpkg`
- new consumer fixtures

Tasks:

1. Align CMake minimum versions and C++23 settings with the repository.
2. Move `GNUInstallDirs` initialization before its variables are used.
3. set `EXPORT_NAME ECS` or otherwise guarantee installed `NGIN::ECS`.
4. Use pre-1.0-compatible package version matching.
5. Keep `NGIN::Base` as the only runtime package dependency.
6. Remove `NGIN.Core` and `NGIN.Reflection` from the NGIN package manifest.
7. Make static linkage the explicit supported `0.2.0` artifact.
8. Remove or isolate unsupported shared-library options and definitions.
9. Avoid force-overwriting parent cache options except inside the package
   wrapper's controlled integration boundary.
10. Add add-subdirectory, install/find-package, and NGIN package consumer
    tests.
11. Verify install, uninstall manifest content, and header completeness.
12. Add an ECS-specific CI workflow or a clearly isolated job in the workspace
    CI.

Exit criteria:

- all three consumption paths build the same sample;
- each path exposes `NGIN::ECS`;
- no hosted runtime or reflection dependency is pulled into a plain consumer;
- installed metadata does not accept an incompatible future minor version.

## WS7: Documentation And Examples

Objective: make the final API understandable without reading implementation
headers.

Primary files:

- `Dependencies/NGIN/NGIN.ECS/README.md`
- `Dependencies/NGIN/NGIN.ECS/docs/`
- `Dependencies/NGIN/NGIN.ECS/examples/`
- new `CHANGELOG.md`

Tasks:

1. Replace `ECS.hpp` with the umbrella header and delete skeleton code/tests.
2. Rewrite the README around a complete, minimal application.
3. Update quick start, entities, storage, queries, systems, and change
   detection for the final names and semantics.
4. Add component requirements and exception-safety documentation.
5. Add invalidation, thread-safety, and world-ownership rules.
6. Add CMake and NGIN package installation examples.
7. Add migration notes from the current prototype.
8. Add `CHANGELOG.md` with the `0.2.0` contract.
9. Compile complete documentation programs in CI.
10. Keep examples small enough to serve as contract tests.

Exit criteria:

- no document mentions a removed alias or manual scheduler build requirement;
- no normal mutation example calls `MarkChanged`;
- every caveat represents a deliberate contract, not unfinished behavior;
- a new user can complete the quick start through either supported package
  path.

## WS8: CI, Performance, And Release

Objective: prove the release contract across supported environments.

Primary areas:

- `.github/workflows/`
- ECS tests, randomized tests, and benchmarks
- package consumer fixtures
- release notes and version metadata

### WS8.1: Test Matrix

Tasks:

- run Debug and Release unit/contract tests;
- run Windows/MSVC, Linux/GCC, Linux/Clang, and macOS/AppleClang;
- run ASan and UBSan where supported;
- run install/find-package and NGIN package consumer tests;
- compile examples and documentation snippets;
- upload CTest and sanitizer logs on failure.

### WS8.2: Randomized Testing

Tasks:

- build a simple reference world model;
- generate deterministic structural and value operation sequences;
- compare liveness, component presence, values, and query results;
- print and preserve failing seeds;
- run a short seed set on every PR and a longer set before release.

### WS8.3: Performance Gates

Tasks:

- warm and repeat every benchmark;
- emit machine-readable samples and summary statistics;
- measure memory per entity;
- compare against the WS0 baseline;
- investigate regressions beyond the master-plan thresholds;
- document accepted regressions with the correctness or usability benefit.

### WS8.4: Release Candidate

Tasks:

1. Freeze the public headers.
2. Run the complete release matrix.
3. Run the extended randomized seed set.
4. Run benchmark and memory regression gates.
5. Build and run all examples from installed artifacts.
6. Validate the NGIN package in a plain native consumer.
7. Audit installed headers for accidental detail exposure.
8. Audit documentation for stale prototype names.
9. Set all version metadata to `0.2.0`.
10. Complete changelog, release notes, and compatibility statement.
11. Mark every P0 and P1 tracker item complete.

Exit criteria:

- every release gate in the master plan passes;
- no P0 or P1 issue remains open;
- the release candidate can be consumed without source-tree-relative paths.

## Recommended Change Sequence

Use these reviewable implementation slices:

1. Baseline tests, sanitizer reproduction, and benchmark stabilization.
2. Chunk allocation RAII and failure-injection tests.
3. Transactional row/world transitions and lifetime tests.
4. Strong `EntityId` and component metadata rules.
5. Typed query terms and views.
6. Structural iteration guard and automatic change tracking.
7. Command storage and flush safety.
8. Deferred entity tokens.
9. Scheduler registration, dirty planning, and ordering.
10. CMake install/export and package dependency repair.
11. Umbrella header, documentation, examples, and migration notes.
12. Randomized tests, CI matrix, performance gates, and release candidate.

Each slice should leave the focused test suite passing. Public API slices may
temporarily require examples and docs to change in the same commit because
pre-release compatibility is not being preserved.

## Post-Release Backlog

These items are intentionally deferred unless implementation evidence makes one
necessary for correctness:

- parallel execution of conflict-free stages;
- resources and scheduler-injected context;
- query plan caching beyond what benchmarks justify;
- relationships and hierarchies;
- serialization IDs and reflection integration;
- shared-library ABI support;
- allocator and chunk-size configuration;
- richer performance comparisons with other ECS libraries;
- visual scheduler graph tooling.
