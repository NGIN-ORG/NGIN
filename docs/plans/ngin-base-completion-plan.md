# NGIN.Base Completion and Expansion Plan

Status: Proposed
Created: 2026-08-03
Scope: `Dependencies/NGIN/NGIN.Base`, its package wrapper, and direct consumers

## Purpose

This plan turns the current NGIN.Base audit into a sequence of reviewable,
independently verifiable changes. It covers correctness work, reusable platform
facilities, completion of existing APIs, component packaging, performance
facilities, documentation, and CI.

The work should not be delivered as one large refactor. Each milestone should
leave the repository buildable and should introduce only the compatibility
needed for the current supported API. Superseded or speculative legacy paths
must not be added.

## Goals

- Correct the synchronization API and give every synchronization primitive
  focused coverage.
- Provide one cross-platform process API and remove duplicated CLI process
  implementations.
- Make the network layer usable with textual addresses, DNS, and TLS.
- Complete important filesystem operations across supported platforms.
- Turn the current component metadata into independently usable build and
  package targets.
- Complete the reclamation policies exposed by `ConcurrentHashMap`.
- Add pool-oriented allocators and allocator diagnostics.
- Add genuinely chunk-fed JSON and XML event parsing.
- Document all public NGIN.Base areas and test their include surfaces.
- Exercise supported platforms, providers, sanitizers, and installed packages
  in CI.

## Non-goals

- Replacing standard-library types without a demonstrated NGIN requirement.
- Adding a hidden global scheduler, resolver, process manager, allocator, or
  TLS runtime.
- Adding shell parsing to the normal process API.
- Implementing application protocols such as HTTP, WebSocket, or QUIC as part
  of the TLS milestone.
- Reintroducing old manifest or package compatibility behavior.
- Adding new third-party dependencies without a separate approval decision.

## Engineering rules

- Follow `Dependencies/NGIN/NGIN.Base/AGENTS.md` for all Base changes.
- Keep public APIs in `include/NGIN/` and non-trivial or platform-specific
  implementations in `src/`.
- Use typed errors and explicit ownership.
- Keep asynchronous runtimes explicit.
- Add positive, negative, boundary, and cancellation tests as applicable.
- Add benchmarks only where the change has a performance contract.
- Batch each milestone before running its targeted verification pass.
- Escalate to broader workspace verification only at integration boundaries.

## Dependency order

```text
Baseline and boundary inventory
              |
              v
     Synchronization repair
              |
      +-------+----------------+-------------------+
      |                        |                   |
      v                        v                   v
 Process execution      Addressing and DNS   Filesystem completion
      |                        |
      v                        v
 CLI migration                TLS
      |                        |
      +-------------+----------+
                    |
                    v
          Real component libraries
                    |
        +-----------+------------+
        |                        |
        v                        v
 Concurrent map and memory   Incremental parsing
        |                        |
        +-----------+------------+
                    |
                    v
          Documentation and CI
```

CI additions should accompany the milestone they protect instead of waiting
until the final documentation and CI pass.

## Milestone 0: Baseline and boundary inventory

### Objective

Establish the contracts and dependency boundaries that later milestones must
preserve.

### Work

- [x] Inventory every public header under `include/NGIN/`.
- [x] Assign every public header and compiled source to one component:
  - Foundation
  - Execution
  - IO
  - Serialization
  - Crypto
  - Net
- [x] Record allowed component dependency directions.
- [x] Identify headers that currently depend on a higher-level component.
- [x] Define which headers are central, experimental, or implementation detail.
- [x] Add a compile-only check for every public header.
- [x] Record current focused test and benchmark targets by component.
- [x] Record a baseline for library sizes and key performance benchmarks.
- [x] Document conventions for:
  - typed errors
  - cancellation
  - allocator ownership
  - synchronous and asynchronous pairs
  - platform capability reporting

### Deliverables

- Component ownership table.
- Dependency-boundary check usable by CMake or a repository script.
- Public-header compile matrix.
- Baseline verification report.

### Completion criteria

- Every public header has an owner.
- Every compiled source belongs to exactly one intended component.
- Boundary violations are known before the real component split begins.

## Milestone 1: Synchronization correctness

### Objective

Correct the current try-lock behavior and make the synchronization layer safe
to build upon.

### Work

- [x] Change `ReadWriteLock::TryStartRead()` to return `bool`.
- [x] Change `ReadWriteLock::TryStartWrite()` to return `bool`.
- [x] Audit all wrappers around standard mutex and semaphore operations for
  correct `noexcept` declarations.
- [x] Check PascalCase and standard Lockable API consistency.
- [x] Verify lock-guard move and ownership behavior.
- [x] Add focused tests for:
  - `Mutex`
  - `RecursiveMutex`
  - `SharedMutex`
  - `ReadWriteLock`
  - `Semaphore`
  - `SpinLock`
  - `TicketLock`
  - `LockGuard`
  - `SharedLockGuard`
  - `AtomicCondition`
- [x] Cover successful and failed try-lock operations.
- [x] Cover reader/writer exclusion and multi-reader behavior.
- [x] Add bounded contention and wake-up tests.
- [x] Add TSAN execution for supported concurrency tests.
- [x] Add Sync documentation and examples.

### Likely files

- `include/NGIN/Sync/*.hpp`
- `tests/Sync/*.cpp`
- `tests/CMakeLists.txt`
- `docs/Sync.md`

### Completion criteria

- No try-lock API silently discards acquisition failure.
- Every public Sync primitive has focused behavioral coverage.
- Supported TSAN tests report no new races.

## Milestone 2: Cross-platform process execution

### Objective

Create one reusable process facility and replace the platform-specific process
implementations duplicated by the CLI.

### Proposed ownership

Use `NGIN::IO` because the facility depends on paths, native handles, stream
redirection, and asynchronous execution. It may depend on Execution according
to the current component direction.

### Proposed public surface

- `ProcessOptions`
- `ProcessEnvironment`
- `Process`
- `ProcessResult`
- `ProcessError`
- `RunProcess()`
- `RunProcessAsync()`

### Required behavior

- [x] Accept an executable and an argument vector without shell interpolation.
- [x] Support an optional working directory.
- [x] Support environment inheritance, replacement, and overrides.
- [x] Support inherited, captured, redirected, or discarded standard streams.
- [x] Support separate stdout and stderr capture.
- [x] Support incremental output observers.
- [x] Support configurable output-size limits.
- [x] Support timeout and cancellation.
- [x] Support process groups on POSIX and job objects on Windows.
- [x] Support graceful termination followed by forced termination.
- [x] Report exit codes, signals, timeouts, and cancellations distinctly.
- [x] Preserve Unicode executable, argument, environment, and path data on
  Windows.
- [x] Define behavior when the executable cannot be found or started.

### Implementation sequence

- [x] Add platform-neutral types and typed errors.
- [x] Implement the Windows backend.
- [x] Implement the POSIX backend.
- [x] Add deterministic fixture executables.
- [x] Add synchronous execution tests.
- [x] Add capture, redirection, environment, and working-directory tests.
- [x] Add timeout, cancellation, and process-tree tests.
- [x] Add asynchronous execution tests.
- [x] Migrate `Tools/NGIN.CLI/src/Build.cpp`.
- [x] Migrate `Tools/NGIN.CLI/src/Tooling.cpp`.
- [x] Remove the duplicated CLI implementations after parity is proven.
- [x] Document the distinction between direct execution and explicit shell
  execution.

### Completion criteria

- CLI process behavior remains compatible with its current supported contract.
- The CLI contains no independent Windows/POSIX process-spawning subsystem.
- Process tests pass on Windows, Linux, and macOS.

## Milestone 3: Network addressing and DNS

### Objective

Make the socket layer usable with ordinary textual addresses and hostnames.

### Address value work

- [x] Add strict IPv4 parsing.
- [x] Add strict IPv6 parsing.
- [x] Add allocation-free formatting where practical.
- [x] Add `ToString()` convenience formatting.
- [x] Add equality, ordering where useful, and hashing.
- [x] Decide and document IPv4-mapped IPv6 behavior.
- [x] Add IPv6 scope identifier support if supported by the endpoint model.
- [x] Add endpoint parsing and formatting.
- [x] Support bracketed IPv6 endpoints.
- [x] Reject ambiguous or out-of-range ports.

### Resolver work

- [x] Define `ResolveOptions`.
- [x] Define `ResolvedAddress` and resolver error details.
- [x] Add synchronous hostname and service resolution.
- [x] Support address-family and socket-type filtering.
- [x] Preserve deterministic result metadata without promising OS result order.
- [x] Design an explicit asynchronous resolver driver or executor contract.
- [x] Add asynchronous resolution with timeout and cancellation.
- [x] Avoid a hidden global resolver thread pool.
- [x] Map native resolver failures into `NetError` without losing diagnostics.

### Testing

- [x] Cover valid and invalid IPv4.
- [x] Cover compressed, expanded, and invalid IPv6.
- [x] Cover endpoint round trips.
- [x] Cover loopback and numeric resolution without external network access.
- [x] Use controlled resolver fixtures for multi-result and failure tests.
- [x] Cover cancellation and timeout.

### Completion criteria

- Callers can connect using a parsed address or a resolved hostname.
- Tests do not depend on public internet availability.
- Async resolution uses explicitly owned runtime resources.

## Milestone 4: TLS transport

### Approval gate

TLS changes provider integration and may require an additional third-party
library target such as `OpenSSL::SSL`. Review and approve the provider plan
before implementation.

Approved on 2026-08-05. The implementation follows
`ngin-base-tls-provider-design.md`.

### Recommended initial provider

Define a backend-neutral public contract and implement an OpenSSL-compatible
provider first. Treat native Windows and Apple TLS providers as separate
follow-up implementations rather than mixing multiple provider designs into
the initial change.

### Proposed public surface

- `TlsContext`
- `TlsClientOptions`
- `TlsServerOptions`
- `TlsStream`
- `TlsError`
- trust and certificate-validation policy types

### Work

- [x] Define client and server context ownership.
- [x] Integrate TLS as an `IByteStream` filter.
- [x] Add client and server handshakes.
- [x] Add SNI.
- [x] Add ALPN configuration and negotiated-protocol reporting.
- [x] Add certificate-chain validation.
- [x] Add hostname verification.
- [x] Support custom trust stores.
- [x] Support client certificates and mutual TLS.
- [x] Consume existing `TlsCredentialMaterial` where appropriate.
- [x] Support handshake timeout and cancellation.
- [x] Support clean TLS shutdown.
- [x] Distinguish transport, protocol, certificate, hostname, and provider
  failures.
- [x] Update the OpenSSL package wrapper and provider diagnostics as approved.

### Testing

- [x] Use a local loopback client and server.
- [x] Commit only non-secret test certificate material.
- [x] Cover a trusted certificate.
- [x] Cover an untrusted issuer.
- [x] Cover an expired certificate.
- [x] Cover a hostname mismatch.
- [x] Cover required and missing client certificates.
- [x] Cover ALPN success and mismatch.
- [x] Cover fragmented reads and writes.
- [x] Cover cancellation and timeout during handshake.
- [x] Avoid internet-dependent tests.

### Completion criteria

- TLS composes with existing byte streams and framing filters.
- Default client validation does not silently accept untrusted or wrong-host
  certificates.
- Provider availability and missing capabilities are diagnosable.

## Milestone 5: Filesystem completion

### Objective

Complete important filesystem semantics and bring supported platforms closer
to behavioral parity.

### Work

- [x] Add a race-free no-replace rename primitive where supported.
- [x] Define atomic replacement options and durability guarantees.
- [x] Implement Windows symlink target reading.
- [x] Implement Windows recursive copying.
- [x] Implement cross-mount VFS copying.
- [x] Implement cross-mount VFS moving as copy followed by source deletion.
- [x] Delete a move source only after the copy completes successfully.
- [x] Define partial-output cleanup behavior.
- [x] Define symlink following and preservation options.
- [x] Define overwrite, metadata, and permission-copy behavior.
- [x] Bring synchronous and asynchronous operations into parity.
- [x] Add cancellation points for recursive and cross-mount work.

### Testing

- [x] Cover no-replace success and conflict.
- [x] Cover recursive files and directories.
- [x] Cover symlinks and dangling symlinks where platform permissions allow.
- [x] Cover cross-mount copy with two mounted temporary filesystems.
- [x] Cover failed cross-mount move preserving the source.
- [x] Cover cancellation and partial cleanup.
- [x] Cover Windows-specific behavior in Windows CI.

### Completion criteria

- Supported operations no longer return the known v1 or Windows
  not-implemented errors.
- Move failure cannot silently destroy the source.
- Atomicity and durability claims are documented precisely.

## Milestone 6: Real component libraries and packages

### Approval gate

This is a broad build, package, and ownership restructuring. Review the final
component graph and consumer migration list before changing target semantics.

Approved on 2026-08-05. The implementation follows
`ngin-base-component-migration-design.md`.

### Objective

Replace transitional component metadata with independently usable compiled,
installed, and packaged targets.

### Work

- [ ] Confirm the final dependency graph from Milestone 0 data.
- [ ] Replace metadata-only component targets with compiled targets.
- [ ] Provide supported static and shared forms.
- [ ] Avoid duplicate compilation and duplicate symbols in the aggregate.
- [ ] Make `NGIN::Base` an aggregate over the real components.
- [ ] Export and install every public component target.
- [ ] Update `NGINBaseConfig.cmake`.
- [ ] Update `Packages/NGIN.Base/NGIN.Base.nginpkg` with component exports.
- [ ] Add installed-package consumer tests.
- [ ] Add build-tree consumer tests.
- [ ] Add a dependency-boundary enforcement check.
- [ ] Ensure component targets propagate only their required platform and
  provider dependencies.

### Consumer migration order

- [ ] CLI model and authoring libraries.
- [ ] CLI resolution, build, tooling, and commands.
- [ ] Reflection and MetaGen.
- [ ] Core and Log.
- [ ] ECS.
- [ ] UI and UI integrations.
- [ ] Examples and remaining package wrappers.

### Compatibility policy

- Keep `NGIN::Base` as the current aggregate convenience target.
- Use narrow component targets for new and migrated code.
- Do not add alternative legacy target spellings or silent fallback targets.

### Completion criteria

- A consumer can link one component without linking the complete Base binary.
- Installed and build-tree targets behave consistently.
- The package manifest exposes the supported component targets.
- Boundary checks prevent dependency direction regressions.

## Milestone 7: Concurrent map reclamation

### Objective

Give each exposed `ConcurrentHashMap` reclamation policy its actual documented
behavior.

### Work

- [x] Document the guarantees of `ManualQuiesce`.
- [x] Implement dedicated local-epoch reclamation.
- [x] Implement dedicated hazard-pointer reclamation.
- [x] Avoid hidden process-global reclamation state.
- [x] Define thread registration and deregistration.
- [x] Define reader pinning and nested-reader behavior.
- [x] Define behavior for stalled or abandoned threads.
- [x] Define map destruction with active or retired nodes.
- [x] Add deterministic reclamation counters or hooks for tests.
- [x] Review interaction with custom allocators.

### Testing and benchmarks

- [x] Test each policy independently.
- [x] Test erased-node lifetime.
- [x] Test reader/writer concurrency.
- [x] Test resize during active readers.
- [x] Test destruction and forced drain.
- [x] Add bounded stress tests.
- [x] Add TSAN jobs.
- [x] Benchmark read-heavy, write-heavy, mixed, and reclamation-heavy workloads.

### Completion criteria

- `LocalEpoch` and `HazardPointers` no longer share the scaffolded retirement
  behavior.
- Readers cannot observe reclaimed storage.
- Retired objects are reclaimable at documented synchronization points.

## Milestone 8: Allocator and memory facilities

### Objective

Add the pool-oriented and diagnostic allocators already identified as useful
extensions of the allocator model.

### Work

- [x] Add a fixed-size freelist allocator.
- [x] Add a segregated pool allocator.
- [x] Add a debug canary and poisoning decorator.
- [x] Add a reallocation convenience helper.
- [x] Evaluate a typed fixed-block object pool after the allocator primitives
  are proven.
- [x] Make each allocator conform to `AllocatorConcept`.
- [x] Keep allocator ownership explicit.
- [x] Keep thread safety an explicit wrapper or policy.
- [x] Document size, alignment, capacity, and upstream-allocation behavior.

### Testing and benchmarks

- [x] Test exhaustion and reuse.
- [x] Test alignment boundaries.
- [x] Test allocator composition.
- [x] Test constructor failure and rollback.
- [x] Test invalid-free diagnostics in supported debug modes.
- [x] Test thread-safe compositions.
- [x] Benchmark intended workloads against `SystemAllocator` and
  `LinearAllocator`.

### Completion criteria

- New allocators compose with existing allocator-aware containers.
- Debug behavior is deterministic and does not change release semantics.
- Benchmarks demonstrate the workloads for which each allocator is intended.

## Milestone 9: Chunk-fed JSON and XML parsing

### Objective

Add parsers that can retain state across arbitrary input chunks while keeping
the existing contiguous APIs intact.

### Proposed contract

- `Feed(bytes)`
- `Finish()`
- statuses equivalent to:
  - `NeedMoreInput`
  - `EventProduced`
  - `Complete`
  - `Error`
- explicit lifetime rules for values spanning chunks
- persistent source positions, limits, and diagnostics

### JSON work

- [x] Define the incremental JSON state machine.
- [x] Preserve partial UTF-8 sequences.
- [x] Preserve partial escapes and surrogate pairs.
- [x] Preserve partial strings, numbers, literals, arrays, and objects.
- [x] Preserve duplicate-key policy behavior.
- [x] Add JSONL-friendly reset behavior.

### XML work

- [x] Define the incremental XML state machine.
- [x] Preserve partial names, attributes, and text.
- [x] Preserve partial entities and character references.
- [x] Preserve comments, CDATA, declarations, and processing instructions.
- [x] Preserve namespace and trivia policies.
- [x] Keep unsupported external-entity behavior explicit.

### Testing and fuzzing

- [x] Split every existing corpus input at every possible byte boundary.
- [x] Compare incremental events with contiguous-parser events.
- [x] Randomize chunk boundaries in fuzz targets.
- [x] Split UTF-8 sequences and escape sequences deliberately.
- [x] Verify limits remain global across all feeds.
- [x] Verify `Finish()` rejects incomplete documents.
- [x] Verify source offsets and diagnostics across chunks.

### Completion criteria

- Chunk partitioning does not change accepted semantics or emitted events.
- Resource limits cannot be bypassed by feeding many small chunks.
- Existing contiguous parser contracts remain available and tested.

## Milestone 10: Documentation and public include policy

### Objective

Make the complete public surface discoverable without turning one umbrella
header into an accidental dependency on every subsystem.

### Documentation work

- [x] Foundation and primitives.
- [x] Utilities and typed error handling.
- [x] Meta and hashing.
- [x] Units and math.
- [x] Sync.
- [x] Execution, tasks, threads, and fibers.
- [x] SIMD.
- [x] Containers.
- [x] IO, filesystem, and Process.
- [x] Network, addressing, DNS, and TLS.
- [x] Serialization.
- [x] Crypto.

### Umbrella-header policy

- [x] Define the intended scope of `NGIN/NGIN.hpp`.
- [x] Prefer it as a Foundation convenience surface rather than an include for
  all optional subsystems.
- [x] Add or confirm focused subsystem umbrellas:
  - `NGIN/Execution.hpp`
  - `NGIN/IO.hpp`
  - `NGIN/Net.hpp`
  - `NGIN/Serialization.hpp`
  - `NGIN/Crypto/Crypto.hpp`
- [x] Add include-contract tests for every umbrella.
- [x] Ensure public examples use the narrowest practical include surface.

### Completion criteria

- Every supported public area is reachable from the documentation index.
- Every public header compiles independently.
- Umbrella headers have documented and tested ownership boundaries.

## Milestone 11: CI and release readiness

### Objective

Exercise the supported contract across platforms, compilers, providers, and
installation modes.

### Final CI matrix

- [x] Windows with MSVC.
- [x] Linux with GCC.
- [x] Linux with Clang.
- [x] macOS with AppleClang.
- [x] ASan and UBSan on supported Linux tests.
- [x] TSAN for synchronization and concurrent-container tests.
- [x] SSE2 and AVX2 SIMD configurations.
- [x] ARM/NEON through an available ARM runner.
- [x] OpenSSL provider tests.
- [x] libsodium provider tests.
- [x] TLS provider tests.
- [x] Bounded parser fuzzing.
- [ ] Static component build.
- [ ] Shared component build.
- [ ] Installed-package consumer tests.

### Release-readiness checks

- [ ] Review exported symbols and visibility.
- [ ] Review package target names and dependency propagation.
- [x] Review error and cancellation consistency.
- [x] Review documentation examples against built APIs.
- [x] Record known platform limitations.
- [x] Record benchmark changes for performance-sensitive work.

### Completion criteria

- Every declared supported operating system runs the relevant Base tests.
- Provider-dependent features fail at configure time or report capabilities
  clearly at runtime as documented.
- Installed consumers can use both aggregate and component targets.

## Proposed change-set sequence

Keep the work in reviewable changes, approximately as follows:

1. [x] Baseline ownership and header checks.
2. [x] Sync fixes and focused tests.
3. [x] Process API and platform implementations.
4. [x] CLI process migration.
5. [x] Address parsing and formatting.
6. [x] DNS resolution.
7. [x] Filesystem atomic and Windows operations.
8. [x] Cross-mount VFS operations.
9. [x] TLS public design and provider decision.
10. [x] TLS provider implementation.
11. [ ] Real component targets and installation.
12. [ ] Consumer target migration.
13. [x] Concurrent-map reclamation.
14. [x] Pool allocators and debug decorators.
15. [x] Incremental JSON parser.
16. [x] Incremental XML parser.
17. [ ] Documentation and final CI completion.

## Verification strategy

### Per change

- Build only the affected Base test targets first.
- Run the matching CTest label or name filter.
- Run sanitizers or benchmarks only when relevant to that change.
- Compile the affected public include-contract tests.

### Integration boundaries

Run broader verification after:

- CLI process migration.
- TLS provider integration.
- Filesystem behavior completion.
- Component target migration.
- Incremental parser completion.

At those boundaries, use the standalone Base build and the directly affected
workspace targets. Run the full workspace flow only where build composition or
shared behavior has changed.

## Risks and controls

| Risk | Control |
| --- | --- |
| Process behavior changes CLI output or cancellation | Migrate only after parity tests cover both old call sites |
| DNS introduces hidden blocking | Require an explicit resolver runtime for asynchronous use |
| TLS expands dependency scope | Separate provider approval and keep the public API backend-neutral |
| Cross-mount move loses data | Copy completely, validate success, then delete the source |
| Component split creates duplicate symbols | Use one ownership list and aggregate real component targets |
| Consumer migration becomes too broad | Migrate one ownership group per change set |
| Reclamation races are nondeterministic | Add deterministic hooks, stress tests, and TSAN |
| Incremental parsing bypasses limits | Persist limits across feeds and fuzz chunk boundaries |
| Umbrella headers increase coupling | Define focused subsystem umbrellas and compile contracts |

## Definition of done

The plan is complete when:

- [ ] All milestone completion criteria are satisfied.
- [ ] No known `NGIN.Base` not-implemented path covered by this plan remains.
- [x] The CLI uses the shared Process API.
- [x] Addressing, DNS, and TLS are usable through explicit runtime contracts.
- [x] Filesystem behavior is documented and tested across supported platforms.
- [ ] Component targets are independently buildable, installable, and
  consumable.
- [x] Concurrent-map policies provide their named reclamation behavior.
- [x] Pool allocators and diagnostics are integrated with the allocator model.
- [x] JSON and XML accept arbitrary chunk boundaries through their incremental
  APIs.
- [ ] Public documentation and include tests cover all supported areas.
- [ ] CI exercises supported platforms and relevant provider configurations.
- [x] Targeted and integration verification results are recorded for each
  delivered change set.
