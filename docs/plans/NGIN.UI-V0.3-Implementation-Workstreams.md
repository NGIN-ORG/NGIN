# NGIN.UI Version 0.3 Implementation Workstreams

Status: Planned execution map
Roadmap: [`NGIN.UI-V0.3-Roadmap.md`](NGIN.UI-V0.3-Roadmap.md)

## Purpose

This document turns the version 0.3 MVVM roadmap into bounded engineering
workstreams. Public API names remain provisional until the matching milestone
contract is approved. The plan adds no dependency, manifest schema, markup
language, or second async runtime.

## Ownership Boundaries

| Concern | Primary ownership |
|---|---|
| Observable values, bindings, commands, validation, task scopes | `Packages/NGIN.UI/` |
| Coroutine, scheduler, cancellation, and combinator primitives | Existing `NGIN.Async` ownership boundary |
| Composition, controls, and presentation hosts | `Packages/NGIN.UI/` |
| Shared MVVM demonstrations | `Examples/NGIN.UI.Gallery/` |
| Deterministic product coverage | `Examples/NGIN.UI.Gallery.Tests/` |
| Hosted lifecycle smoke coverage | `Examples/NGIN.UI.Gallery.Hosted/` |
| Developer guides and plans | `docs/guides/` and `docs/plans/` |

Native backend packages supply event loops and windows but must not own MVVM
behavior. Generated output under `build/` is verification evidence, never an
implementation surface.

## Workstream A — Command Contracts

Status: Completed in Milestone 26 on 2026-08-02.

### Goal

Make user actions observable and safe without replacing simple callbacks.

### Contract Decisions

- command value versus interface and ownership model;
- typed parameters and results, if any;
- read-only binding shape for `CanExecute` and execution state;
- synchronous error representation;
- subscription and invalidation behavior;
- control integration without overload ambiguity.

### Verification

- availability propagation and control enablement;
- subscriber removal and command destruction;
- action success and failure;
- no allocation or subscription leaks across repeated composition.

## Workstream B — AsyncCommand Execution

Status: Completed in Milestone 26 on 2026-08-02.

### Goal

Run coroutine actions with explicit scheduling, cancellation, concurrency, and
error observation.

### Contract Decisions

- how a command obtains or is given its UI task scope;
- reject, cancel-previous, and bounded-queue concurrency semantics;
- outcome/error representation and retry behavior;
- cancellation races and UI-scheduler continuation rules;
- treatment of exceptions from user coroutine bodies.

### Verification

- completion, failure, explicit cancellation, and owner cancellation;
- rapid repeated execution under every policy;
- late completion after destruction;
- application shutdown and window closure;
- no continuation from painting or a worker thread.

## Workstream C — Derived State And Batching

Status: Completed in Milestone 27 on 2026-08-02.

### Goal

Represent read-only and computed ViewModel values without mirrored writable
state.

### Contract Decisions

- read-only observable type and conversion from `State<T>`;
- explicit dependency declaration and equality/change suppression;
- batching boundary and invalidation coalescing;
- cycle detection and diagnostic behavior;
- lifetime of subscriptions captured by derived values.

### Verification

- single and multiple dependencies;
- unchanged values and batched mutations;
- nested derivation, cycle rejection, and teardown;
- deterministic notification and composition counts.

## Workstream D — Validation

Status: Completed in Milestone 27 on 2026-08-02.

### Goal

Provide typed field and form validation that commands and controls can observe.

### Contract Decisions

- validation issue identity, severity, message, and field association;
- immediate, deferred, and submit-time policies;
- async validation cancellation and stale-result versioning;
- aggregate form state and focus/semantics integration;
- whether control visuals consume a generic validation presentation contract.

### Verification

- issue insertion, replacement, ordering, and clearing;
- synchronous and asynchronous validation;
- rapid edits and out-of-order completion;
- form aggregation, command availability, and accessibility output.

## Workstream E — ViewModel Task Scope And Lifecycle

Status: Completed in Milestone 28 on 2026-08-02.

### Goal

Give every UI-bound coroutine an explicit owner and cancellation boundary.

### Contract Decisions

- scope construction from `Application::CreateTaskContext()`;
- operation tracking, observation, and shutdown behavior;
- opt-in activation/deactivation contract without mandatory inheritance;
- keyed view-host ownership and reuse rules;
- maximum cleanup behavior during window/application shutdown;
- service and ViewModel factory hooks.

### Verification

- activation once, reuse, replacement, and deactivation;
- cancellation on unmount, popup/tab closure, window closure, and shutdown;
- activation/deactivation races and late completion;
- failure observation and task-drain diagnostics.

## Workstream F — Async Presentation

Status: Completed in Milestone 28 on 2026-08-02.

### Goal

Make asynchronous screen states clear without allowing composition to suspend.

### Contract Decisions

- typed idle/loading/content/empty/error representation;
- host control versus composition helper;
- retry and cancel command integration;
- focus retention, semantics, and keyed identity between states;
- reduced-motion transitions without changing lifecycle timing.

### Verification

- every presentation state and transition;
- cancellation, failure, retry, and stale results;
- focus, keyboard, accessibility, and narrow layout;
- headless deterministic composition.

## Workstream G — Gallery, Documentation, And Packaging

### Goal

Make the complete MVVM path discoverable, copyable, and consumable.

### Deliverables

- `GalleryViewModel` naming and a complete editable async workflow;
- focused command, validation, loading, cancellation, and retry examples;
- architecture, first-screen, async-command, validation, and lifetime guides;
- public API comments and source-compatibility notes;
- standalone, hosted, and headless parity;
- install/export consumer and supported-platform smoke evidence;
- documented allocation, subscription, and outstanding-task diagnostics.

## Execution Order

### Wave 1 — Command Safety

1. Approve the command, outcome, and read-only observation contracts.
2. Implement Workstreams A and B.
3. Add the focused Gallery command example and docs.
4. Run targeted command/task verification.
5. Close and commit Milestone 26.

### Wave 2 — Observable ViewModel State

1. Approve derived-state, batching, and validation contracts.
2. Implement Workstreams C and D.
3. Add the editable validation Gallery example and docs.
4. Run targeted state/validation verification.
5. Close and commit Milestone 27.

### Wave 3 — Lifetime And Presentation

1. Approve task-scope, activation, host ownership, and shutdown rules.
2. Implement Workstreams E and F.
3. Add the asynchronous navigation/presentation Gallery example and docs.
4. Run targeted lifetime/presentation verification.
5. Close and commit Milestone 28.

### Wave 4 — Product Completion

1. Complete Workstream G.
2. Evaluate optional boilerplate reduction against the stable manual API.
3. Run package and supported-platform release gates.
4. Close and commit Milestone 29.

Milestones must remain separate commits. Do not begin the next wave with an
uncommitted completed milestone.

## Verification Matrix

| Change area | Minimum verification |
|---|---|
| Command | focused command tests and headless button binding |
| AsyncCommand | task completion/cancellation/error/concurrency tests |
| Derived state | dependency, batching, cycle, and teardown tests |
| Validation | sync/async race tests and Gallery form smoke |
| Task scope | unmount/window/shutdown lifetime tests |
| Async presentation | headless state transitions and native Gallery smoke |
| Accessibility | validation and loading semantic-tree tests |
| Public API | docs/API comments, install/export consumer, compatibility review |
| Product completion | standalone, hosted, headless, and supported-platform Gallery matrix |

Use one targeted verification pass per milestone and escalate only when its
ownership boundary or a failure requires broader coverage.

## Milestone Closure Checklist

- the public contract and lifetime rules are written before implementation;
- implementation stays inside the correct ownership boundary;
- no second task runtime, string binding system, or mandatory base class was
  introduced;
- focused tests cover success, failure, cancellation, destruction, and races;
- the Gallery demonstrates the feature through public APIs;
- documentation describes actual behavior and limitations;
- generated build output was not edited;
- targeted verification passes and evidence is recorded;
- the milestone is committed before work starts on the next milestone.
