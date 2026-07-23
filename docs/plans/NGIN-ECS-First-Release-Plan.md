# NGIN.ECS First Release Plan

Status: Proposed implementation contract

Target: `NGIN.ECS 0.2.0`

`0.2.0` is the first supported public release of `NGIN.ECS`. The existing
pre-release API may break while this plan is implemented. Compatibility starts
at the released `0.2.0` surface, not at the current skeleton and prototype
surface.

Execution details live in
[`NGIN-ECS-First-Release-Workstreams.md`](NGIN-ECS-First-Release-Workstreams.md).
Day-to-day status lives in
[`NGIN-ECS-First-Release-Progress.md`](NGIN-ECS-First-Release-Progress.md).

## Summary

`NGIN.ECS` already has a useful archetype-based core: generational entities,
structure-of-arrays chunks, typed query terms, row-level change ticks, deferred
commands, a deterministic serial scheduler, focused tests, examples, and a
benchmark executable.

The first release should preserve that core and replace the prototype edges
around it. The release is not a storage-engine rewrite. It is a contract,
correctness, safety, packaging, and usability overhaul.

The release must make these statements true:

- A new user can include one header, create a world, spawn entities, query
  components, and run systems without discovering legacy placeholder APIs.
- Query declarations are authoritative. Read-only queries cannot acquire
  mutable component access through a generic row or chunk view.
- Mutable access participates in change detection automatically.
- Structural mutation cannot invalidate an active query silently.
- Deferred commands preserve alignment, object lifetime, ordering, and failure
  semantics for all supported component types.
- Scheduler plans cannot become stale after system registration.
- The standalone CMake package, installed CMake package, and NGIN package
  wrapper expose the same target and dependency contract.
- Supported compilers and operating systems run the same behavioral test suite.
- The public API has an explicit compatibility policy and no legacy skeleton
  helpers.

## Product Position

`NGIN.ECS` is a compact, type-safe C++23 ECS for NGIN applications and ordinary
CMake consumers. It favors explicit data access, deterministic execution, and
predictable archetype storage over runtime reflection or a large feature set.

The first release is intended for:

- game and simulation runtime state;
- tools that process many similarly shaped objects;
- NGIN-hosted or plain native applications;
- users who want typed components without registration macros or inheritance.

The first release is not intended to compete on feature count with mature,
general-purpose ECS frameworks. It should instead provide a small surface whose
correctness and behavior can be trusted.

## First Release Scope

The supported feature set is:

- strong, generational entity handles;
- normal C++ component values, including empty tags and move-only values;
- archetype and chunk storage;
- spawn, despawn, add, remove, replace, read, and mutable access;
- typed read, write, optional, presence, absence, added, and changed query
  terms;
- row and chunk iteration with access constrained by query terms;
- deferred structural commands;
- deterministic serial system scheduling with access conflict analysis,
  explicit ordering, and structural barriers;
- row-level added and changed tracking;
- standalone CMake consumption, CMake installation, and NGIN package
  consumption;
- focused tests, sanitizer coverage, examples, and repeatable microbenchmarks.

## Explicit Non-Goals

The following do not block `0.2.0`:

- parallel system execution;
- relationships, hierarchies, or graph queries;
- prefabs or entity cloning;
- events or message buses;
- resource or singleton injection;
- serialization, replication, or persistence;
- reflection-generated component registration;
- runtime component types unknown to C++;
- stable binary ABI across library versions;
- hot reload;
- deterministic entity values across processes;
- lock-free world mutation;
- shipping shared-library support.

These features may be added after the core contract has real users. The first
release must not include placeholder APIs for them.

## Current Baseline

The implementation currently lives under
[`Dependencies/NGIN/NGIN.ECS`](../../Dependencies/NGIN/NGIN.ECS), with its NGIN
package wrapper under
[`Packages/NGIN.ECS`](../../Packages/NGIN.ECS).

Existing strengths:

- `World` owns entity locations and archetype storage.
- `Archetype` and `Chunk` provide SoA storage with row-level added and changed
  ticks.
- `Query<Terms...>` supports row and chunk iteration.
- `Commands` supports deferred spawn, despawn, add, remove, set, and clear.
- `Scheduler` derives component access from typed system parameters.
- Tests are already divided by ECS concern.
- Documentation explains the current mental model and caveats.

Release-blocking issues found in the current baseline:

- [`ECS.hpp`](../../Dependencies/NGIN/NGIN.ECS/include/NGIN/ECS/ECS.hpp) exposes
  `ParseInt` and `LibraryName` instead of the ECS.
- Generic `RowView::Write<T>` and `ChunkView::Write<T>` can mutate a component
  that the query declared as `Read<T>`, invalidating scheduler conflict data.
- Mutable access does not mark a component changed, so a forgotten
  `MarkChanged<T>` silently breaks change-driven systems.
- Query views reference iteration scratch state and do not have an explicit
  callback-only lifetime contract.
- Immediate structural changes during iteration can invalidate storage without
  a guard.
- `Commands` places non-trivial operations in `Vector<std::byte>`. The base
  allocation is not guaranteed to satisfy over-alignment, and vector growth can
  byte-relocate live non-trivial operation objects.
- `Commands::Flush` does not define a safe cleanup path when an operation
  throws.
- Chunk construction can leak earlier column allocations if a later allocation
  throws before the chunk constructor completes.
- Replacement and row migration need explicit exception guarantees.
- `Scheduler::Build()` is manual, so registering a system after a build can
  leave a stale plan that `Run()` accepts.
- System names are borrowed `const char*` values rather than owned names.
- The installed CMake export is not proven to produce the same `NGIN::ECS`
  target as the build tree.
- The NGIN package wrapper declares `NGIN.Core` and optional
  `NGIN.Reflection` dependencies even though the source target links only
  `NGIN::Base`.
- Public debug storage methods and raw archetype access blur the supported API
  boundary.
- The one-shot benchmark does not provide a stable performance baseline.

## Public API Contract

### Header Surface

`#include <NGIN/ECS/ECS.hpp>` is the supported umbrella include. It includes
the normal user surface:

- `Entity.hpp`
- `World.hpp`
- `Query.hpp`
- `Commands.hpp`
- `Scheduler.hpp`

Focused includes remain supported for compile-time-sensitive users.

Storage implementation headers are moved under `NGIN/ECS/Detail`.
`Archetype`, `Chunk`, component operation tables, and query scratch structures
are not normal user APIs. Detail headers may be installed because templates
depend on them, but they are excluded from the compatibility contract.

`ParseInt`, `LibraryName`, their source implementation, and their skeleton
tests are removed.

### Naming

The release follows the existing NGIN PascalCase method convention. Duplicate
lowercase aliases such as `each`, `read`, and `for_chunks` are removed before
release. There is one documented spelling for each operation.

Terminology is normalized on **tick**, not a mixture of tick and epoch:

- `CurrentTick()`
- `PreviousTick()`
- `AdvanceTick()`
- `SinceTick()`

### Entity Handles

`EntityId` becomes a strong value type instead of an alias to `UInt64`.

Required properties:

- default construction produces the null entity;
- equality, ordering, hashing, and an explicit boolean validity check;
- no implicit conversion to or from integers;
- an implementation-defined 64-bit packed representation;
- index and generation access only through named diagnostic helpers;
- stale handles remain invalid after despawn and slot reuse;
- exhausted generation slots are retired instead of wrapping into a value that
  can make an ancient handle live again.

Entity handles are scoped to their originating `World`. Passing a handle to
another world is invalid usage. The packed 64-bit handle does not carry a world
identifier, so users must not rely on cross-world misuse being detected.
Cross-world stable identity is outside the release contract.

### Component Contract

A component type must be:

- an object type;
- destructible;
- move constructible or copy constructible;
- safely relocatable according to its C++ traits or an explicit NGIN
  bitwise-relocation trait.

The implementation must prefer copying when a move can throw and copying is
available. Operations that cannot provide a safe storage transition must fail
at compile time with a component-specific diagnostic.

Additional rules:

- duplicate component types in one spawn or query are compile-time errors;
- cv/ref qualifiers are normalized to the underlying component type;
- empty components are tags and have no mutable data access;
- component IDs include a diagnostic type name;
- a registry detects an ID collision with incompatible type metadata and fails
  clearly;
- IDs are process-local implementation keys, not serialization identifiers.

### World

The normal immediate API remains compact:

```cpp
World world;

EntityId entity = world.Spawn(Position {}, Velocity {});
world.Add<Health>(entity, Health {100});
world.Remove<Velocity>(entity);
world.Set<Position>(entity, Position {1.0f, 2.0f});
world.Despawn(entity);
```

Direct access has throwing and non-throwing forms:

```cpp
const Position* position = world.TryGet<Position>(entity);
const Position& required = world.Get<Position>(entity);

Position* mutablePosition = world.TryGetMutable<Position>(entity);
Position& mutableRequired = world.GetMutable<Position>(entity);
```

Acquiring mutable access marks the component changed at the current tick.
False-positive change notification is accepted; silent false negatives are
not. `Set<T>` also marks changed. An explicit `MarkChanged<T>` remains available
for interior mutation performed through another owner, but normal writes do not
require it.

Structural operations are forbidden while any query is iterating the same
world. Checked builds report a clear contract violation. Systems use `Commands`
for structural mutation.

`World::Archetypes()` and `DebugGet*` are removed from the public world API.
Tests and diagnostics use a separate internal test accessor or a narrow,
read-only `WorldStats` snapshot.

### Queries

The query declaration is the authority for component access.

Supported terms:

- `Read<T>`: requires `T` and exposes `const T&`;
- `Write<T>`: requires `T` and exposes `T&`;
- `Optional<T>`: does not require `T` and exposes `const T*`;
- `With<T>`: requires presence without exposing data;
- `Without<T>`: requires absence;
- `Added<T>`: filters by the added tick;
- `Changed<T>`: filters by the changed tick.

Conflicting terms are compile-time errors. Examples include `Read<T>` with
`Write<T>`, `With<T>` with `Without<T>`, and required access with
`Optional<T>`.

Iteration uses a query-typed row:

```cpp
Query<Write<Position>, Read<Velocity>, Without<Disabled>> query {world};

query.ForEach([](auto row) {
    Position& position = row.Get<Position>();
    const Velocity& velocity = row.Get<Velocity>();
    position.x += velocity.x;
});
```

`row.Get<T>()` has its constness constrained by the corresponding query term.
`row.TryGet<T>()` exists only for `Optional<T>`. Filter-only terms grant no
component access. Attempting undeclared or mutable access fails at compile time.

The advanced chunk API follows the same typed access rules and is named
`ForEachChunk`. A chunk view cannot be converted into an untyped mutable view.

Rows, chunk views, references, and pointers obtained from iteration are valid
only for the duration documented by the callback and until structural mutation.
They are non-owning and must not be stored. Row and chunk view types are
non-copyable and callback scoped.

`Added<T>` and `Changed<T>` are filters, not access grants. To inspect the
component, users combine them with `Read<T>` or `Write<T>`:

```cpp
Query<Read<Position>, Changed<Position>> changedPositions {world};
```

### Change Detection

Change detection is row and component granular.

The rules are:

- spawn marks each initial component as added at the current tick;
- add marks the new component as added at the current tick;
- mutable access marks the component changed at the current tick;
- set marks the component changed at the current tick;
- migration preserves existing components' added and changed ticks;
- swap removal preserves the moved row's ticks;
- scheduled queries compare against that system's last successful run tick;
- direct queries default to `PreviousTick()` and may accept an explicit
  `sinceTick`;
- tick comparison remains correct across unsigned tick rollover.

The scheduler advances a world exactly once at the start of each successful run
attempt. Manual users call `AdvanceTick()` explicitly.

### Deferred Commands

`Commands` remains the structural-mutation mechanism used during systems and
query iteration.

Required behavior:

- operation payloads have correct alignment;
- growing the buffer never byte-relocates a live non-trivial object;
- move-only and over-aligned component values are supported;
- queued operation order is deterministic;
- destroying or clearing an unflushed buffer destroys each payload exactly
  once;
- flush destroys each completed payload exactly once;
- if an operation throws, completed operations remain completed, the failing
  and remaining payloads are cleaned up exactly once, and the buffer becomes
  empty before the exception escapes;
- commands targeting stale entities follow the same checked/non-throwing policy
  as immediate world operations.

Deferred spawn returns a buffer-local token:

```cpp
DeferredEntity entity = commands.Spawn(Position {});
commands.Add<Health>(entity, Health {100});
```

The token can be targeted by later commands in the same buffer. It cannot be
used as a live `EntityId` before flush or retained after the buffer is reset.

`ClearWorld` is removed from deferred commands for `0.2.0`. Clearing a world is
an explicit immediate operation performed outside iteration or through an
exclusive system.

### Systems And Scheduler

The first release scheduler is deterministic and serial. It computes stages and
barriers but does not claim parallel execution.

The normal user path is:

```cpp
Scheduler scheduler;

SystemId move = scheduler.AddSystem("Move", [](
    Query<Write<Position>, Read<Velocity>>& query) {
    query.ForEach([](auto row) {
        row.Get<Position>().x += row.Get<Velocity>().x;
    });
});

scheduler.Run(world);
```

Required behavior:

- `AddSystem` owns the system name and callable through
  `std::move_only_function`;
- registration returns a strong `SystemId`;
- query parameters derive read and write sets;
- `Commands&` creates a structural barrier;
- unrestricted `World&` access is possible only through an explicitly
  exclusive system;
- registration or ordering changes mark the plan dirty;
- `Run` rebuilds and validates a dirty plan automatically;
- `Build` remains as an optional eager validation operation, not a required
  correctness step;
- duplicate names, invalid IDs, and ordering cycles produce actionable
  diagnostics;
- explicit `Before` and `After` constraints are supported through `SystemId`;
- conflicts and explicit ordering preserve deterministic registration order;
- a failed system does not update its last successful run tick;
- pending commands are cleaned safely if a system throws.

Stage internals are not returned as `std::vector<int>`. A read-only diagnostics
surface may expose system names, IDs, access sets, stages, and ordering reasons.

### Errors And Checked Builds

Normal absence uses non-throwing APIs:

- `IsAlive`
- `Has<T>`
- `TryGet<T>`
- `TryGetMutable<T>`
- `Remove<T>` returning `bool`

Precondition failures in throwing APIs use documented standard exception types.
Messages identify the failed operation and component type; checked builds also
include the packed entity value. Internal invariant failures use NGIN
assertions in checked builds and are not presented as recoverable user errors.

The release documents which operations can throw and their state guarantee:

- no effect;
- valid but partially applied state;
- completed prefix for command flush.

No public operation may leave a component slot containing a destroyed object
that the world still considers live.

## Storage And Correctness Invariants

The refactor must preserve these invariants:

- every live entity has exactly one valid location;
- every stored row belongs to exactly one live entity;
- the entity slot and chunk row agree after swap removal and chunk compaction;
- an archetype signature is sorted, unique, and collision checked;
- every non-tag component object is constructed exactly once and destroyed
  exactly once;
- every allocation is deallocated with the same size and alignment;
- a failed chunk constructor releases all allocations made before failure;
- a failed row insertion rolls back all constructed destination columns;
- migration does not remove the source row until the destination row is valid;
- tags consume no per-row component storage;
- tick columns remain synchronized with component rows;
- an active query prevents structural mutation of its world;
- active query guards are restored on every normal and exceptional callback
  exit.

The existing SoA archetype model and default chunk-size strategy remain. A
configurable allocator or chunk size may be exposed through `WorldConfig` only
after the implementation can route all world-owned allocations consistently
through `NGIN::Memory::PolyAllocatorRef`. Partial allocator customization is
not a release feature.

## Packaging And Build Contract

### Dependencies

The public source target depends on `NGIN::Base` only.

The NGIN package manifest removes required `NGIN.Core` and optional
`NGIN.Reflection` dependencies unless real code integration is added in a later
release. Plain CMake and plain NGIN applications must be able to consume
`NGIN.ECS` without the hosted runtime or reflection system.

### CMake

The supported CMake contract is:

```cmake
find_package(NGINECS CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE NGIN::ECS)
```

Required fixes and checks:

- align the minimum CMake version with the repository-supported version;
- include `GNUInstallDirs` before using its variables;
- set the installed export name so the target is `NGIN::ECS`;
- use an appropriate pre-1.0 package-version compatibility policy;
- export and find `NGINBase` consistently;
- verify build-tree, add-subdirectory, and installed consumption;
- make test dependency acquisition opt-in and reproducible;
- avoid forcing unrelated parent-project cache options where possible;
- make the supported linkage explicit.

`0.2.0` officially supports static linkage. Shared-library builds are either
disabled or clearly marked unsupported until symbol export and cross-boundary
template behavior have dedicated CI coverage. The NGIN package manifest and
CMake behavior must agree.

### Supported Toolchains

Release CI covers:

- Windows with MSVC;
- Linux with GCC;
- Linux with Clang;
- macOS with AppleClang.

All supported configurations use C++23 with compiler extensions disabled.

## Documentation Contract

Documentation is rewritten against the final API, not patched around prototype
caveats.

Required documents:

- a concise README with installation and a complete first example;
- a ten-minute quick start;
- entity and world lifecycle;
- query terms and typed access;
- commands and structural mutation;
- systems, ordering, and scheduler behavior;
- change detection and tick semantics;
- component requirements and object lifetime;
- errors, invalidation, and thread-safety;
- CMake and NGIN package consumption;
- migration notes from the pre-release prototype;
- a changelog for `0.2.0`.

Every documentation snippet that represents a complete program is compiled in
CI, directly or through the examples that own it.

## Test Strategy

### Unit And Contract Tests

Tests cover:

- null, live, stale, recycled, cleared, and retired entity handles;
- generation and index boundary behavior;
- duplicate component rejection;
- empty, trivial, non-trivial, move-only, copy-only, over-aligned, large, and
  throwing component types;
- chunk allocation cleanup and row rollback;
- spawn, despawn, add, remove, set, migration, swap removal, and clear;
- every query term and valid term combination;
- compile-time rejection of undeclared writes and conflicting terms;
- automatic changed marking;
- tick rollover and per-system baselines;
- nested read-only iteration and structural-mutation rejection;
- row and chunk view invalidation contracts;
- command ordering, alignment, growth, move-only payloads, deferred entities,
  clear, destruction, and throwing flush;
- scheduler access conflicts, explicit ordering, cycles, dirty rebuild,
  barriers, exceptions, and repeated runs;
- installed package consumption and NGIN package consumption.

### Randomized Model Testing

A deterministic randomized test compares `World` behavior against a simple
reference model across sequences of spawn, despawn, add, remove, set, clear, and
query operations. Seeds are printed on failure and a fixed regression seed list
is kept in the repository.

### Sanitizers And Diagnostics

Release candidates pass:

- AddressSanitizer;
- UndefinedBehaviorSanitizer on supported Clang/GCC jobs;
- compiler warnings at the repository's strict warning level;
- leak detection where the CI platform supports it.

ThreadSanitizer is not a release gate because the first release world is
single-threaded.

### Performance

The benchmark harness is changed from one-shot wall-clock output to repeated,
warmed measurements with machine-readable results.

Baseline scenarios:

- spawn into one archetype;
- iterate read-only;
- iterate read/write with change tracking;
- add/remove migration;
- despawn;
- command recording and flush.

Performance is compared with the baseline captured before the API refactor.
Absent a documented reason:

- query iteration may not regress by more than 10%;
- spawn, migration, despawn, and commands may not regress by more than 15%;
- memory per entity for the baseline archetype may not regress by more than
  10%.

These are regression gates, not marketing claims. Cross-library comparisons are
informational and must use equivalent semantics.

## Compatibility Policy

`0.2.x` follows semantic versioning for the documented source API:

- patch releases fix defects without intentionally breaking documented source
  usage;
- additive APIs normally wait for a minor release unless required to fix an
  unsafe contract;
- `0.3.0` may make source-breaking changes with migration notes;
- no stable binary ABI is promised before `1.0.0`;
- undocumented detail headers and test accessors are not compatibility
  surfaces.

Deprecations are not added for APIs that have never shipped in a supported
release. Prototype names are removed directly before `0.2.0`.

## Release Gates

`0.2.0` is releasable only when all of the following are true:

- the public API described above is implemented and documented;
- no legacy skeleton API remains;
- all known correctness blockers in this plan have regression tests;
- the focused ECS suite passes in Debug and Release;
- ASan and UBSan jobs pass;
- the supported compiler/OS matrix passes;
- standalone add-subdirectory consumption passes;
- installed `find_package(NGINECS)` consumption passes;
- the NGIN package wrapper validates and builds a consumer;
- examples compile and run;
- documentation examples compile;
- benchmark regression gates pass or have reviewed explanations;
- no public header exposes release-internal storage mutation;
- the progress tracker has no open P0 or P1 item;
- release notes and the compatibility policy are published with the tag.

## Definition Of Done

The work is complete when a user can start from either CMake or an NGIN package,
follow the quick start without caveats, write type-safe systems whose declared
access matches actual access, use commands safely during iteration, rely on
change filters without manual bookkeeping, and run the same application on all
supported platforms with the full release test matrix passing.
