# Async coroutine API and propagation redesign

Date: 2026-10-04

Status: technical draft for review; no implementation work is authorized by this document.

Primary owner: `Dependencies/NGIN/NGIN.Base`, Execution component.

## 1. Intended outcome

Make ordinary asynchronous code read as sequential code while preserving explicit
execution ownership, bounded scheduling, cancellation, and predictable resource
costs. The proposed default is `Task<T>` with a common error representation,
automatic propagation, and native awaitables for synchronization primitives.

Illustrative target API; these examples describe proposed behavior and are not
claims about the current implementation:

```cpp
Task<Document> LoadDocument(Path path, AsyncSemaphore& semaphore)
{
    auto permit = co_await semaphore.AcquireAsync();
    auto bytes = co_await ReadFile(path);
    co_return co_await DecodeDocument(bytes);
}
```

Each successful await produces its value. Error or stopped completion terminates
the current task through the runtime's propagation protocol. The caller does not
write a forwarding branch for either outcome. The permit remains owned across
suspension and releases during normal exit or failure cleanup.

Recovery is explicit:

```cpp
Task<Document> LoadWithFallback(Path path, AsyncSemaphore& semaphore)
{
    auto result = co_await Try(LoadDocument(path, semaphore));
    if (result.HasError() && result.Error().Is(FileError::NotFound))
        co_return MakeEmptyDocument();

    co_return co_await Propagate(std::move(result));
}
```

`Try` observes all three outcomes. `Propagate` extracts a success or forwards an
unchanged error/stop outcome. Both are awaiter adapters without helper coroutine
frames. They work for `void` as well as value results. Most functions need neither.

This draft selects the common-error direction for implementation planning. An
improved `Task<T, E>` design remains a comparison prototype in phase 0, not a
second permanent public API. No compatibility period is required for external
consumers. Existing in-repository callers still require a coordinated migration.

## 2. Current constraints and what changes

Relevant sources:

- [Task implementation](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Async/Task.hpp)
  requires identical parent/child domain-error types for ordinary propagation.
- [AsyncSemaphore](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Async/AsyncSemaphore.hpp)
  currently adds typed coroutine wrappers that observe and reconstruct completion.
- [Completion helpers](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Async/Completion.hpp)
  currently let `Faulted`, `Stopped`, and domain-failure awaits record an outcome
  while execution continues after the await.
- [Existing runtime contracts](../../Dependencies/NGIN/NGIN.Base/docs/AsyncRuntime/Contracts.md)
  define ownership, publication, executor placement, and structured cleanup.
- [Current optimization plan](../../Dependencies/NGIN/NGIN.Base/docs/AsyncRuntime/OptimizationPlan.md)
  records unfinished performance work and platform verification.

The existing contracts explicitly retain `Task<T, E>` and typed scope/race errors.
This proposal intentionally conflicts with those parts. They remain current
until implementation migrates them; do not edit them to claim the new API exists.
Their lifetime, admission, and scheduling guarantees remain requirements unless
an individual change is explicitly documented and tested.

Preserve:

- Cold, move-only, single-consumer tasks and allocation-free ready task machinery.
- Root ownership through scopes/supervisors and the blocking root bridge.
- Direct sequential child transfer, bounded continuation depth/work, and reserved
  delivery for admitted work after ordinary executor admission closes.
- Inherited executor, cancellation, allocation resource, and logical metadata.
- Distinct value, error, and stopped outcomes, including existing stop reasons.
- Explicit executor transitions and the existing placement behavior of children.
- Separation of backend completion, terminal publication, and retirement.

Replace:

- Error-type parameters on tasks, operations, completions, scopes, and supervisors.
- Forwarding-only branches and coroutine wrappers used to reconcile error types.
- Completion-setting awaiters whose apparent terminal operation continues running.
- Task wrappers around primitive waits when an awaiter can perform the operation.

Out of scope: a new scheduler, OS I/O backend rewrite, eager task startup, global
runtime, exception-free compiler mode, detached work, or changes to manifest and
package composition semantics. Improvements to existing I/O performance gates
remain separately tracked.

## 3. Public model

The following names are the draft vocabulary. Final signatures are frozen in
phase 0 before broad migration.

| Surface | Proposed contract |
| --- | --- |
| `Task<T>` | Cold coroutine or inline ready outcome; ordinary await propagates error/stop |
| `Completion<T>` | Value-owned `Value`, `Error`, or `Stopped`; no pending alternative |
| `Error` | Common domain/code/native information with optional owned diagnostics |
| `Operation<T>` | Move-only result interest in independently owned execution |
| `TaskScope`, `TaskSupervisor` | Existing ownership responsibilities without an error template parameter |
| `Try(awaitable)` | Consume an NGIN awaitable and return `Completion<T>` without propagating its outcome |
| `Propagate(completion)` | Consume a completion; produce its value or terminate the current task |
| `Fail(error)` | Terminal awaitable for explicit failure in both value and void tasks |
| `Stop(reason)` | Terminal awaitable for explicit stopped completion |
| `Task<T>::FromValue`, `FromCompletion` | Preserve explicit ready representations |

`Try` and `Propagate` do not start independent operations or change executors. A
stopped result returned by `Try` does not clear the inherited cancellation token.
A subsequent cancellation-aware await may stop again. Cleanup that must finish
uses the existing non-abandonable join protocol; this API does not imply arbitrary
cancellation masking.

`Try` is an outcome-observation adapter, not a C++ catch-all expression. Exceptions
thrown while evaluating its argument before construction follow ordinary C++
rules. Exceptions arising while a supported operation is being awaited are
handled at the task/operation boundary described below.

Replace `.AsCompletion()` with the uniform `Try` form at final migration, rather
than retaining two public spellings. The adapter initially supports NGIN tasks,
owned-operation waits, and opted-in NGIN primitive awaitables. Foreign C++ awaiters
require an explicit adapter to participate in NGIN error/stop propagation; do not
reinterpret every third-party `await_resume()` value as a completion.

Explicit failure:

```cpp
Task<void> Validate(Input input)
{
    if (!input.IsValid())
        co_await Fail(Error::From(ValidationError::InvalidInput));

    // Executed only when validation succeeds.
    co_await Persist(input);
}
```

Use one terminal failure form for `Task<void>` and `Task<T>`. Do not promise that
`co_return error` works uniformly: C++ coroutine promises cannot provide both
`return_void` and `return_value` for the same promise type. Terminal awaits must
actually leave the body, rather than requiring a following `co_return`.

### Typed domain results

Use `Task<NGIN::Utilities::Expected<T, E>>` when a domain outcome is deliberately
part of the returned value and the caller should exhaustively inspect it. Ordinary
await returns that `Expected` unchanged. An unexpected domain value inside it is
still a successful task completion; cancellation and runtime failures remain
outer task outcomes.

Do not automatically unwrap `Expected`, introduce another task family, or hide
that distinction behind an implicit conversion. Most I/O operations should use
ordinary common-error propagation; typed result values are a deliberate API choice.

## 4. Common error representation

Build on the information already represented by
[`Utilities::ErrorInfo`](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Utilities/Error.hpp).
Do not simply erase every error to a message string or `std::exception_ptr`.

Required properties:

1. A compact inline identity: domain identifier, code, and native code where
   relevant. Constructing, moving, matching, or forwarding code-only errors does
   not allocate.
2. Typed construction and matching, illustrated by `Error::From(MyError::Code)`
   and `error.Is(MyError::Code)`. Traits map enums to domain identity and codes
   without RTTI or mutable global registration.
3. Application-defined domains do not require adding enumerators to NGIN's current
   closed `ErrorDomain` enum. Identity must also work across static/shared library
   boundaries; an address of a template-local static is insufficient as the sole
   cross-module identity contract.
4. Optional immutable owned diagnostics for messages, native detail, exception
   capture, and aggregate scope/race reports. Borrowed strings and payloads cannot
   escape their owners. Diagnostic ownership must not form a cycle with the
   operation that owns the error. Escaping diagnostic storage must retain an
   allocator owner or use a resource documented to outlive every retained error
   and report; joining a task does not make its borrowed PMR resource immortal.
5. Moving errors is nonthrowing. Copying a code-only error or retaining existing
   immutable diagnostic ownership is allocation-free. Rich diagnostic creation is
   an explicit failure-path cost; success never creates diagnostic ownership.
6. Ordinary forwarding preserves the same error, including native detail and
   diagnostics. Do not reformat, copy rich payloads, or allocate at each await.

Put reusable error identity, traits, and storage in Foundation, alongside existing
error utilities. Execution supplies async fault codes and maps runtime failures
into that representation. Foundation must not depend on Async, Execution, IO, or
Net. IO, Net, TLS, UI, and application mappings belong to their own layers.

Phase 0 must settle the extensible domain identifier, diagnostic access/type
identity mechanism, and error storage size with concrete IO, Net, and scope-report
examples. Prefer a fixed, compact representation over a general heterogeneous
metadata container. Exact layout is not frozen in this draft.

### Failure during error creation

Resource exhaustion must be representable without allocating. If rich diagnostic
construction fails, preserve the original code when available and omit optional
detail; otherwise publish a code-only resource-exhaustion/runtime error. Do not
recursively allocate to report allocation failure.

Unexpected C++ exceptions inside a coroutine still reach `unhandled_exception()`
and become a runtime error. Retain exception details only under the configured
capture policy. This proposal avoids throwing for normal propagation; it does not
claim zero cost for thrown exceptions or support for `-fno-exceptions`.

## 5. Completion storage and propagation

Use one discriminated outcome storage abstraction for value, error, or stop reason.
Share the outcome logic between void and non-void promises; only value storage and
language-required return methods need specialization. Measure its size against
the existing separate optional value/error storage.

Inline ready tasks remain supported. No successful result gains a shared control
block simply to unify its type with errors. Shared immutable storage belongs at
concurrent observation/reporting boundaries that actually need it.

Implement a single internal completion-routing protocol used by child task awaits,
native primitive awaits, explicit terminal awaits, and observation adapters:

| Outcome | Normal await | `Try` await |
| --- | --- | --- |
| Value | Resume the body with `T`, or continue for `void` | Resume with successful `Completion<T>` |
| Error | Terminate the enclosing task with the same `Error` | Resume with error `Completion<T>` |
| Stopped | Terminate the enclosing task with the same stop reason | Resume with stopped `Completion<T>` |

Failure handling belongs in this protocol, not in a handwritten branch at every
caller. Ordinary helpers do not need additional coroutine frames to translate
outcomes. Necessary recovery, error translation, and scope aggregation remain
explicit operations.

Terminal propagation is a runtime state transition, not an exception thrown from
`await_resume()`. It must:

1. Claim exactly one terminal outcome and prevent later body resumption.
2. Retire child/backend/callback access that must finish before locals can be
   destroyed or borrowed resources can be released.
3. Destroy active coroutine locals exactly once, including guards on bypassed
   failure paths, before an awaiting owner is allowed to release their resources.
4. Route completion through the existing continuation/retirement machinery with
   bounded stack use and the existing executor placement/fairness guarantees.

Do not call `handle.destroy()` on an executing frame or propagate through an
unbounded recursive chain. Audit the current promise/frame ownership protocol;
renaming `MarkFinishedAndResume` is not proof that cleanup is correct. Observation
paths must retire a child before returning an outcome to code that may destroy
resources that child borrowed.

For a ready non-success outcome, routing must still get access to the parent
promise. An awaiter cannot report ready and then discover in `await_resume()` that
it needs to terminate a parent it never captured. A valid implementation performs
the inline decision in `await_suspend()` and returns without suspension on success.
That incurs no mandatory enqueue or heap allocation.

## 6. Native awaitables and AsyncSemaphore

Keep `Task` as the abstraction for coroutine computations. Use native awaitables
for permit acquisition, yields, delays, and other primitives whose state machine
does not require a coroutine body. Begin with AsyncSemaphore; converting every
I/O leaf at once is unnecessary.

Draft semaphore surface:

```cpp
AsyncSemaphore semaphore(initialCount, maximumCount);
auto permit = co_await semaphore.AcquireAsync();
co_await semaphore.WaitAsync(); // Manual ownership: pair success with Release().
```

Both wait methods return lazy, move-only, single-consumer awaitables, rather than
`Task` objects. Construction does not acquire or enqueue. `AcquireAsync` produces
the existing move-only permit guard. Remove the `<E>` overloads. `TryAcquire`,
`Release`, `CurrentCount`, count preconditions, and semaphore lifetime obligations
retain their current meanings.

The internal operation/awaiter protocol must expose logical environment binding,
immediate completion, pending registration, terminal notification, and retirement.
Use constrained template glue for promise integration and compiled non-template
state machinery where practical. A stable internal protocol is sufficient; do not
publish a general third-party execution framework as part of this migration.

### Available permit path

On await, bind the current environment, observe cancellation, and attempt the
acquisition under the semaphore's bookkeeping lock. Success returns the guard
directly to the awaiting coroutine. The primitive itself creates no coroutine
frame, heap state, cancellation registration, or completion reservation and performs
no enqueue. The caller's coroutine/root may have its own separately measured costs.

### Pending path

1. Establish stable waiter storage and guaranteed continuation delivery before
   making the waiter visible. Failure here consumes no permit.
2. Register cancellation and recheck availability under the queue lock. Account
   for synchronous cancellation callbacks and a release during setup.
3. Enqueue in FIFO order if still pending. Release and cancellation compete to
   claim the waiter once. Cancellation unlinks it without consuming a permit.
4. Completion notification and setup commit form a two-sided handshake. Neither
   side may resume the coroutine until initialization is fully published.
5. Deliver through the inherited executor outside internal locks. Unregister or
   synchronize callbacks and retire borrowed frame access before exposing success
   or non-success to application code.

Keep the current semaphore rule: once release grants a permit, cancellation does
not revoke it, including while delivery is queued. Grant order is FIFO; observable
execution order on a multi-worker executor need not be FIFO. Return a permit owned
by an unconsumed successful result during result destruction.

Prefer waiter state embedded in the suspended frame only if callback retirement
and frame lifetime can be proven together. Otherwise use one bounded PMR-backed
pending state. Reuse a root continuation slot only when exclusive ownership and
delivery guarantees permit it. Do not make zero pending allocations an acceptance
requirement at the expense of correctness.

Native awaitables are not automatically root tasks. Initially, `Spawn`/`SyncWait`
continue accepting tasks. Code that needs a standalone primitive wait uses a small
explicit task at that ownership boundary. Ordinary coroutine callers avoid that
wrapper. Generalizing root admission to all awaitables is a separate decision.

## 7. Structured ownership, cancellation, and cleanup

`WithScope`, scope reports, `WhenAll`, `Race`, deadlines, and supervisors migrate
to the common error model. Preserve their joining, winner selection, error
precedence, deterministic report ordering, and shutdown behavior. A race winner
still does not allow its losers to outlive borrowed resources.

Aggregate failures carry the primary common error plus retained structured
diagnostics for secondary errors and winner/body information. Removing error
template parameters must not discard the information currently represented by
`ScopeFailure` and `RaceFailure`. Aggregation allocation is bounded by admitted
work and must have an allocation-failure path.

No destructor awaits. A scope helper can retain owned captures through child
joining, but it cannot extend the lifetime of arbitrary automatic variables inside
a callback coroutine. Child resources must be owned by the scope/captures, live
in the enclosing caller, or be explicitly joined before those locals leave scope.
Keep low-level owner destruction with live work fail-fast in release builds.

`Fail`/`Stop` and automatic propagation skip ordinary following statements.
Asynchronous cleanup therefore requires an explicit helper/join protocol that
observes the outcome, completes cleanup, and then propagates it. Synchronous RAII
destruction remains automatic. Do not advertise catch/finally semantics that C++
coroutines cannot supply.

Cancellation stays cooperative. Copying an environment/token does not create a
new linked source or register callbacks. Link only at actual ownership or explicit
cancellation boundaries. A stop request cannot replace an already-published
terminal result.

Preserve each backend's existing arbitration point. In particular, an OS request
finishing is not necessarily the same event as task terminal publication; some
current file paths allow cancellation to win before delivery. This proposal does
not silently change that behavior to match the semaphore's grant rule. Deadline
metadata alone remains separate from timer installation/enforcement.

## 8. Migration ownership and sequence

Implement within the existing component structure. Public headers and sources
must be assigned to their owning component; install and shared-library checks are
part of completion. Changes inside NGIN.Base are submodule changes. When commits
are explicitly requested, commit that repository before updating its workspace
pointer; this draft does not request commits or branches.

| Phase | Work | Exit evidence |
| --- | --- | --- |
| 0: contract and baseline | Freeze common error storage/identity, terminal and observation semantics, actual size/allocation baselines; compare a minimal typed-propagation prototype | Reviewed signatures, representative caller examples, source-identified baseline, lifetime state diagram and controlled-race test plan |
| 1: error foundation | Add common error identity/storage and IO/Net/runtime mappings; preserve structured detail | Code-only allocation tests, rich-payload lifetime/failure tests, cross-component/static/shared identity tests |
| 2: task protocol | Introduce common completion routing, `Task<T>`, `Try`, `Propagate`, `Fail`, `Stop`; migrate coupled scope/operation machinery sufficiently to compile | Ready/pending, value/void, deep failure, RAII, throwing move, resource exhaustion, and stop-propagation tests |
| 3: primitive proof | Convert AsyncSemaphore to native awaitables using the shared routing protocol | Existing semaphore behavior preserved; no primitive frame/heap/enqueue on available permits; controlled races and sanitizers pass |
| 4: composition and callers | Complete combinator/report migration and update IO, Net/TLS, runtime bridge, Core/UI, examples, tests, and benchmarks | No forwarding-only error-type adapters remain in migrated paths; domain detail and placement preserved; real consumers build/run |
| 5: closure | Remove obsolete typed task surfaces and helpers, finalize docs, check package exports and platform matrix | Correctness, performance, packaging, and documentation gates recorded; no obsolete public compatibility path |

Phases describe dependencies, not a requirement to maintain two public task systems
between commits. Batch inseparable template/API migrations so a checkpoint builds.
Use private prototype targets or mechanical call-site migration where necessary;
do not add a permanent public compatibility layer. Preserve unrelated local work,
including the existing async optimization edits and semaphore implementation.

The caller audit must include at least:

- Base Async: Task/Completion/Operation, scope/supervisor, generators, cancellation,
  observation, transitions, deadlines, races, and root blocking bridges.
- Base IO/IORuntime: `IOResult` aliases, file APIs, process execution, `RunTask`,
  backend completion adapters, runtime teardown, and their tests.
- Base Net/NetTLS: sockets, resolver, transports, TLS and nested error translation.
- `Packages/NGIN.Core` and `Packages/NGIN.UI`: command/validation results,
  presentation scheduling, task ownership, and error reporting.
- Canonical examples, standalone Base examples, benchmarks, documentation, and
  installed-package consumers. Search all first-party submodules for other users
  before claiming migration completeness.

Map every old domain error to a common error or an intentional typed result value.
Do not mechanically replace `Task<T, E>` and lose the meaning of `E`.

## 9. Verification and performance acceptance

This is a proposal, so all implementation gates below are initially unchecked.
Do not reuse earlier test results as evidence for redesigned code.

### Correctness matrix

- [ ] Ready and pending success/error/stopped for void and move-only values.
- [ ] `Try` returns all outcomes; `Propagate` preserves all error detail and stop
  reasons; no extra helper coroutine or task start is introduced.
- [ ] Explicit `Fail`/`Stop` never execute following statements; local guards are
  destroyed once on all propagation paths, with bounded stack use at deep nesting.
- [ ] Exception and throwing-value-move paths become runtime errors and do not
  leak acquired resources; error construction remains safe under allocation failure.
- [ ] Scope/race cleanup preserves primary and secondary outcomes; owned child
  retirement precedes release of resources those children borrow.
- [ ] Semaphores cover FIFO grants, canceled head/middle/tail, simultaneous release
  and cancellation, setup races, delivery rejection, closed executor admission,
  discarded successful results, guard moves, and worker-thread contention.
- [ ] Cancellation and timeout decisions preserve operation-specific publication
  rules; observer cancellation never accidentally cancels independently owned work.
- [ ] Cross-executor completion preserves inherited metadata and selected placement.
- [ ] Root teardown, saturation, escaped result handles, and repeated creation/teardown
  return memory/registration/reservation counts to baseline.
- [ ] Focused Debug, ASan/UBSan, and TSan checks pass for changed concurrency logic.
- [ ] Public-header checks, relevant installed consumers, and static/shared builds
  pass; Linux, Windows, and macOS evidence is reported separately.
- [ ] Workspace callers and relevant Core/UI/I/O examples pass their canonical
  checks. Expand to CLI/manifest tests only if those surfaces change.

### Measurements

Record source hashes and working diffs, compiler, configuration, hardware, exact
commands, and raw outputs. Use the actual working-tree baseline, including existing
local improvements. Keep historical async runtime gates separately attributable.

Measure task/awaiter size, compiler frame size, task construction/destruction,
ready await, pending handoff, cancellation registration, error propagation, spawn
and join, and full teardown. Include chain depths 1/8/64 and stress depths for stack
safety, one/multiple workers, cannot-stop/stoppable tokens, and runtime/external
executor placements. Exercise code-only and rich errors separately.

Count frame/runtime allocations and bytes, outstanding storage, registrations,
reservations, queue hops, and wakeups separately from timing. Include semaphore
uncontended and contended workloads plus existing AsyncRuntime and representative
file/network/mixed workloads. Do not infer overall performance from semaphore
microbenchmarks alone.

Structural gates:

- Ready task machinery remains allocation-free, excluding user payload costs.
- Native available-permit acquisition adds zero coroutine frames, heap allocations,
  registrations, reservations, or queue submissions.
- `Try`/`Propagate` and common-error forwarding add no helper frame, heap allocation,
  or mandatory scheduling hop of their own.
- Pending state is bounded by admitted work and fully retires; rich diagnostic
  retention is observable and does not keep unrelated operation storage alive.
- Success-path size and per-root fixed costs are reported before and after; growing
  all task frames to optimize rare diagnostics requires a specific justification.

Before implementation timing, freeze acceptable latency/throughput deltas against
phase 0 variability; do not choose thresholds after seeing the new results. As a
draft investigation trigger, treat a greater-than-5% shift in a matched primary
metric as needing attribution, rather than automatically calling it a regression
or dismissing it as noise. Existing historical performance thresholds remain in
force and are not replaced by this trigger.

Use serial, matched Release runs with at least five processes per version, rotated
order and unchanged payloads/worker counts. Report distributions and variability,
not only a best run. Do not run builds, sanitizers, profiling, or competing
benchmarks alongside acceptance timing. Performance approval requires repeatable
evidence; improving ergonomics alone does not establish a speedup.

## 10. Decisions to close before implementation

| Decision | Draft direction | Required resolution |
| --- | --- | --- |
| Common error identity and diagnostics | Compact inline identity, optional immutable owned detail, explicit traits | Extensible domain identity across modules; detail access and ownership; measured size and failure behavior |
| Observation spelling | `Try(awaitable)` returning `Completion<T>` | Confirm one canonical spelling and treatment of foreign awaitables |
| Terminal operations | `co_await Fail(error)` / `co_await Stop(reason)` | Prove immediate body termination, destructor order, and scope-helper cleanup using the existing lifetime model |
| Primitive pending storage | Embedded waiter if proven safe; otherwise bounded PMR state | Controlled-interleaving proof and actual allocation/retirement counts |
| Performance thresholds | Structural gates above, matched comparisons against current code | Freeze workload list and numerical allowances from baseline variability |

No broader scheduler or backend contract change is implied by selecting these
APIs. If implementation exposes a conflict with an existing lifetime or delivery
guarantee, resolve it explicitly in this plan and its tests before proceeding.

## 11. Completion criteria

The redesign is complete when normal application composition uses ordinary
`co_await`, recovery uses one explicit observation mechanism, primitives avoid
forwarding-only coroutine wrappers, and common errors retain actionable detail
without allocating on code-only paths. All migrated owners must preserve structured
cleanup and existing execution guarantees.

Publish the implemented contract in Base's per-area docs and public headers,
update runnable examples and workspace guides, and record the durable decision
under `docs/architecture/decisions/`. Retire this plan after its lasting content and
verification evidence have established homes. Until then, the current API docs
remain authoritative for shipped code.
