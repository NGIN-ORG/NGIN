# NGIN.ECS First Release Progress

Status: Implementation complete; external CI matrix pending

Target: `NGIN.ECS 0.2.0`

Last updated: 2026-07-23

This file tracks execution of
[`NGIN-ECS-First-Release-Plan.md`](NGIN-ECS-First-Release-Plan.md) through the
workstreams in
[`NGIN-ECS-First-Release-Workstreams.md`](NGIN-ECS-First-Release-Workstreams.md).

## Current Summary

The first-release implementation is complete in the working tree. The public
API, storage lifetime model, query access rules, deferred commands, scheduler,
packaging, examples, documentation, tests, benchmarks, and CI definitions have
all been replaced or brought to the `0.2.0` contract.

There are no open P0 or P1 implementation blockers. Local Debug and Release
gates pass. A release tag still requires the committed GitHub Actions
sanitizer and supported-platform matrix to execute successfully.

Current phase: `Release candidate verification`

## Fixed Decisions

- [x] Target `0.2.0` as the first supported public release.
- [x] Remove unshipped prototype APIs without compatibility aliases.
- [x] Preserve archetype/SoA storage and C++23.
- [x] Keep `NGIN::Base` as the only runtime dependency.
- [x] Support static linkage for `0.2.0`.
- [x] Keep scheduler execution serial and deterministic.
- [x] Enforce query declarations as the component-access authority.
- [x] Mark changed automatically on mutable access.
- [x] Forbid immediate mutation while an incompatible query is active.
- [x] Use buffer-local deferred entity tokens.
- [x] Keep command buffers non-copyable and non-movable.
- [x] Support nested read-only queries and reject nested mutable queries.
- [x] Own scheduler callables with `std::move_only_function`.
- [x] Treat `Added` and `Changed` as filters.
- [x] Normalize time terminology on ticks.
- [x] Define Windows/MSVC, Linux/GCC, Linux/Clang, and macOS/AppleClang CI.

## Blocker Audit

### P0: Memory Safety And Corruption

- [x] Command payloads use individually aligned allocations and are never
  byte-relocated while alive.
- [x] Throwing command flush has completed-prefix semantics and exactly-once
  cleanup.
- [x] Chunk construction rolls back every component and tick allocation.
- [x] Spawn, set, and archetype migration preserve valid object lifetimes on
  failure.
- [x] Migration performs throwing work before source-consuming no-throw
  relocation.
- [x] Swap removal uses only no-throw relocation and preserves moved-row ticks.
- [x] Active queries guard all immediate structural and mutable world access.

### P1: Silent Contract Failure

- [x] Read-only queries cannot obtain mutable data.
- [x] Mutable access updates changed ticks automatically.
- [x] Scheduler plans rebuild automatically when dirty.
- [x] Duplicate and conflicting query terms fail at compile time.
- [x] `EntityId` and `SystemId` are strong handles with private storage.
- [x] Entity slots have explicit free, alive, and retired states.
- [x] Incompatible component type-ID collisions fail clearly.
- [x] Installed CMake consumption exports `NGIN::ECS`.
- [x] The package wrapper depends only on `NGIN.Base`.

### P2: Release Usability

- [x] `ECS.hpp` is the supported umbrella header.
- [x] Skeleton APIs, sources, and tests are removed.
- [x] Scheduler names are owned and explicit ordering is supported.
- [x] Deferred spawn tokens can be targeted by later commands.
- [x] Prototype lowercase aliases and raw storage escape hatches are removed.
- [x] Documentation and examples use the final API.
- [x] Benchmarks use warmup, repeated medians, JSON records, and regression
  gates.

## Milestone Status

| Workstream | State | Exit Evidence |
| --- | --- | --- |
| WS0 Baseline and contract freeze | Complete | Baseline and contract recorded |
| WS1 Storage and lifetime correctness | Complete | Allocation, lifecycle, failure, and randomized tests |
| WS2 Entity and component contract | Complete | Strong handles, metadata, collision, and compile-fail tests |
| WS3 Queries and change detection | Complete | Typed access, guards, auto-change, and rollover tests |
| WS4 Deferred commands | Complete | Alignment, tokens, reentrancy, and failure-position tests |
| WS5 Systems and scheduler | Complete | Dirty rebuild, ordering, barriers, diagnostics, and failure tests |
| WS6 Build, install, and package integration | Complete | Focused, installed, add-subdirectory, and NGIN consumers |
| WS7 Documentation and examples | Complete | Rewritten docs, migration guide, changelog, and two tested examples |
| WS8 CI, performance, and release | Implemented | Workflow and gates authored; CI execution pending |

## Workstream Checklists

### WS0

- [x] Configure standalone Debug and Release builds.
- [x] Discover the full suite through CTest.
- [x] Record pre-refactor timing and storage baselines.
- [x] Add umbrella and focused-header consumer fixtures.
- [x] Attempt local ASan/UBSan and record the Windows runtime limitation.
- [x] Freeze the public `0.2.0` API contract.

### WS1

- [x] Add storage allocation-failure injection.
- [x] Validate allocation metadata and `alignas(64)`/`alignas(256)` columns.
- [x] Make chunk and row construction rollback complete.
- [x] Make swap removal and chunk compaction no-throw.
- [x] Make spawn, add, remove, migration, set, and clear transactional.
- [x] Cover tags, copy-only, move-only, throwing, large, oversized, and
  explicitly bitwise-relocatable components.
- [x] Add deterministic randomized world/model checks.

### WS2

- [x] Introduce strong `EntityId` with null, hashing, ordering, diagnostics,
  private packing, and slot retirement.
- [x] Normalize component cv/ref identity.
- [x] Validate unique components and safe relocation at compile time.
- [x] Add diagnostic type names and incompatible-ID collision detection.
- [x] Add invalid-component, throwing-destructor, and duplicate-spawn
  compile-fail checks.

### WS3

- [x] Implement `Read`, `Write`, `Optional`, `With`, `Without`, `Added`, and
  `Changed`.
- [x] Validate duplicate and contradictory terms at compile time.
- [x] Constrain row and chunk access to declared query terms.
- [x] Guard query lifetime and restore guards after callback exceptions.
- [x] Support nested reads and reject nested writes.
- [x] Mark writes changed and preserve rollover-safe added/changed filtering.

### WS4

- [x] Replace byte-vector operation storage.
- [x] Preserve payload alignment and lifetime across buffer growth.
- [x] Implement completed-prefix flush failure semantics.
- [x] Reject recording and flush reentrancy.
- [x] Add live and deferred command targets.
- [x] Remove deferred world clear.
- [x] Cover first, middle, and last failure positions and token expiry.

### WS5

- [x] Add `Scheduler::AddSystem` and strong `SystemId`.
- [x] Own names, move-only callables, and diagnostics data.
- [x] Auto-build dirty plans.
- [x] Support explicit before/after edges and cycle diagnostics.
- [x] Model commands and exclusive systems as barriers.
- [x] Define per-system last-successful-tick behavior under exceptions.
- [x] Allow explicit ordering to reverse an implicit access conflict.

### WS6

- [x] Align CMake minimums and initialize GNU install directories.
- [x] Export static `NGIN::ECS` with same-minor package compatibility.
- [x] Find and export `NGINBase` consistently.
- [x] Avoid parent cache mutation and keep tests/examples opt-in.
- [x] Test focused headers, add-subdirectory, install/find-package, and version
  rejection.
- [x] Validate and build a product-first `Hello.ECS.nginproj`.
- [x] Audit installed headers and package metadata.

### WS7

- [x] Rewrite README, quick start, API, entities, storage, queries, commands,
  systems, and change detection documentation.
- [x] Document installation, component requirements, errors, invalidation,
  threading, and prototype migration.
- [x] Add `CHANGELOG.md` with release notes and compatibility policy.
- [x] Build and run QuickStart and ChangeDetection examples in CTest.

### WS8

- [x] Add Windows/MSVC Debug and Release CI.
- [x] Add Linux/GCC, Linux/Clang, and macOS/AppleClang CI.
- [x] Add Linux Clang ASan/UBSan with leak detection.
- [x] Run package consumers and examples in CI definitions.
- [x] Expand randomized coverage to eight fixed seeds.
- [x] Emit machine-readable benchmark output.
- [x] Enforce 15% timing and 10% storage regression limits.
- [x] Complete the source and requirement audit.
- [x] Author release notes and compatibility policy.
- [ ] Observe a passing sanitizer workflow on the committed revision.
- [ ] Observe a passing supported-platform workflow matrix.

## Verification Log

| Date | Revision | Environment | Command/Check | Result | Notes |
| --- | --- | --- | --- | --- | --- |
| 2026-07-23 | Pre-refactor tree | Windows 11, Clang 22.1.8, Debug | One-shot baseline, 100,000 entities | Complete | Timing baseline captured before hot-path changes |
| 2026-07-23 | Working tree | Windows 11, Clang 22.1.8, Debug | `ctest --test-dir build/ngin-ecs-isolated --output-on-failure` | Pass, 56/56 | Includes consumers, examples, randomized tests, and benchmark gate |
| 2026-07-23 | Working tree | Windows 11, Clang 22.1.8, Release | `ctest --test-dir build/ngin-ecs-release-opt -C Release --output-on-failure` | Pass, 55/55 | Includes installed and add-subdirectory consumers |
| 2026-07-23 | Working tree | Windows 11 | `cmake --list-presets=all` | Pass | Configure, build, and test presets parse |
| 2026-07-23 | Working tree | Windows 11 | NGIN CLI validate/build and staged `Hello.ECS.exe` run | Pass | Resolves `NGIN.ECS` and `NGIN.Base` only |
| 2026-07-23 | Working tree | Windows 11, Clang 22.1.8 GNU driver | ASan/UBSan build and Catch2 discovery | Environment limitation | Windows dynamic ASan reports CRT allocator interception failures before test enumeration, including when Base is source-built; Linux sanitizer CI is the release gate |

Compile-fail contracts run during CMake configure and cover duplicate spawn,
invalid relocation, throwing destructors, query conflicts, undeclared query
access, and read-only writes.

## Performance

Baseline and current measurements use the same machine, compiler, build type,
entity count, and benchmark semantics.

| Scenario | Pre-refactor | Current median | Change | Gate |
| --- | ---: | ---: | ---: | --- |
| Spawn | 74,447 us | 83,007 us | +11.5% | Pass, 15% limit |
| Read query | 67,139 us prototype query | 3,111 us | Improved | Informational |
| Write query | Not separated | 3,253 us | Not comparable | Informational |
| Commands | 95,853 us | 106,699 us | +11.3% | Pass, 15% limit |
| Migration | Not recorded | 185,738 us | Not comparable | Informational |
| Despawn | 68,779 us | 73,744 us | +7.2% | Pass, 15% limit |

Storage remains exactly `8,058,960` bytes (`80.5896` bytes/entity) for the
three-component scenario and `6,422,528` bytes (`64.2253` bytes/entity) for
the two-component scenario.

## Release Readiness

- [x] Public API and documentation implemented.
- [x] No legacy skeleton API remains.
- [x] No open P0 or P1 implementation blocker remains.
- [x] Debug and Release focused suites pass locally.
- [x] Add-subdirectory and installed CMake consumers pass.
- [x] The NGIN package consumer validates, builds, and runs.
- [x] Examples and documentation-owned complete programs build and run.
- [x] Local benchmark regression gates pass.
- [x] Release notes and compatibility policy are authored.
- [ ] ASan/UBSan CI passes on the committed revision.
- [ ] Windows, Linux, and macOS compiler/OS CI passes on the committed revision.

The unchecked items are release-publication gates, not missing implementation.

## Deferred Post-Release Work

- [ ] Parallel execution.
- [ ] Resources and injected scheduler context.
- [ ] Relationships and hierarchies.
- [ ] Serialization and reflection integration.
- [ ] Shared-library ABI support.
- [ ] Public allocator and chunk-size configuration.
- [ ] Visual scheduler tooling.
