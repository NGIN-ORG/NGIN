# NGIN.UI Version 0.3 Roadmap

Status: Planned
Theme: Type-safe MVVM and asynchronous applications
Baseline: [`NGIN.UI-V0.2-Roadmap.md`](NGIN.UI-V0.2-Roadmap.md)
Execution map:
[`NGIN.UI-V0.3-Implementation-Workstreams.md`](NGIN.UI-V0.3-Implementation-Workstreams.md)

## Purpose

Version 0.2 provides observable state, two-way bindings, UI-scheduled tasks,
cancellation, asynchronous images, and awaitable motion. Those pieces make
MVVM possible, but applications still have to assemble command state, error
handling, validation, task lifetime, and loading presentation themselves.

Version 0.3 turns those foundations into a small, type-safe MVVM layer. It
keeps ordinary C++ models and synchronous composition while making common
ViewModel work concise and safe.

The release is ordered around application safety:

1. user actions need one command contract for availability, execution,
   cancellation, and errors;
2. derived values and validation must update predictably without string-based
   binding;
3. asynchronous work must stop when its owning ViewModel or view disappears;
4. loading, empty, content, and error states must be easy to present;
5. the Gallery and guides must show the complete pattern with public APIs.

Public names in this roadmap are directional until their milestone contract is
approved. Source compatibility remains governed by
[`ngin-ui-source-compatibility.md`](../policies/ngin-ui-source-compatibility.md).

## Design Rules

- A View remains a synchronous composition function. It must never retain or
  suspend a `Composer`.
- Models remain ordinary C++ types with no required UI inheritance.
- ViewModels may use lightweight helpers but do not require a framework base
  class.
- Bindings and commands remain typed C++ contracts. Version 0.3 does not add
  string paths, runtime property lookup, or a markup language.
- UI-bound task continuations run through an application task context, never
  from painting or a worker thread.
- Detached work must have explicit ownership, cancellation, and error
  observation.
- Headless behavior remains deterministic; native backends do not define MVVM
  semantics.

## Current Gaps

| Area | Version 0.2 | Version 0.3 target |
|---|---|---|
| User actions | Plain callbacks start tasks manually | Typed `Command` and `AsyncCommand` |
| Availability | Controls are enabled manually | Observable `CanExecute` state |
| Execution state | Application-owned flags | Built-in running and cancellation state |
| Errors | Each task chooses its own handling | Observable, non-lossy command outcomes |
| Derived values | Manual state synchronization | Read-only computed observable values |
| Validation | Validators are control-specific | Field and form validation state |
| Lifetime | Raw `TaskContext` ownership | ViewModel/view task scopes |
| Async presentation | Hand-authored conditionals | Reusable loading/content/empty/error pattern |
| MVVM guidance | Architecture is implicit in examples | Complete Gallery sample and developer guide |

## Milestone 26 — Commands And AsyncCommand

Deliver:

- define a backend-neutral `Command` contract with an executable action and
  observable `CanExecute` state;
- define `AsyncCommand` on `NGIN::Async::Task` and application-created task
  contexts rather than introducing another coroutine or scheduler system;
- expose read-only execution state including `IsRunning`, `CanCancel`, and the
  most recent outcome or error;
- provide explicit concurrency policies for rejecting a second execution,
  canceling the previous execution, and bounded queuing, with reject-while-
  running as the safe default;
- link command cancellation to explicit cancellation, ViewModel ownership,
  window closure, and application shutdown;
- contain exceptions and errors so a button callback cannot silently lose an
  asynchronous failure;
- make controls consume command availability and invoke commands without
  application-written enabled-state plumbing;
- preserve ordinary callback overloads for actions that do not need command
  state;
- add deterministic tests for availability changes, completion, failure,
  cancellation, repeated execution, destruction, and UI-scheduler resumption;
- add a focused Gallery example showing run, cancel, failure, retry, and
  double-click protection.

Exit criterion:

A ViewModel can expose a save or load command whose button availability,
running state, cancellation, and error are observable without manually
coordinating flags or detaching an unowned task.

## Milestone 27 — Derived State And Validation

Deliver:

- add a read-only observable value contract for data that a View may consume
  but must not write;
- add computed state with explicit typed dependencies and deterministic change
  propagation;
- avoid hidden global dependency tracking, string property paths, and
  reflection-only binding behavior;
- batch related state changes so one logical ViewModel update does not cause a
  cascade of redundant composition passes;
- define field validation results with severity, message, and stable identity;
- support synchronous validation and cancellation-safe asynchronous validation
  whose stale results cannot overwrite newer input;
- expose aggregate form validity and validation summaries as derived state;
- integrate command availability with derived validity and dirty state without
  creating ownership cycles;
- add tests for dependency changes, batching, cycles, destruction, validation
  races, and error ordering;
- extend the Gallery with a small editable form demonstrating immediate,
  deferred, server-style asynchronous, and summary validation.

Exit criterion:

A ViewModel can express values such as `FullName`, `IsDirty`, `CanSave`, and
validation messages without duplicating writable state or manually notifying
every dependent control.

## Milestone 28 — ViewModel Lifetime And Async Presentation

Deliver:

- introduce an owning UI task scope that wraps a UI-scheduled `TaskContext`,
  cancellation source, and observation of every started operation;
- make the scope usable by a plain ViewModel through composition rather than
  mandatory inheritance;
- define optional activation and deactivation hooks for ViewModels that need
  asynchronous loading or cleanup;
- specify exactly when activation runs and when cancellation occurs across
  keyed reconciliation, tab changes, popup closure, window closure, and
  application shutdown;
- ensure slow cleanup cannot indefinitely block window or application
  shutdown;
- add a reusable presentation contract for idle, loading, content, empty, and
  error states, including retry and cancellation actions;
- keep `Compose(Composer&)` synchronous: lifecycle tasks update observable
  state and normal invalidation composes the resulting presentation;
- provide narrow service-resolution and ViewModel-factory hooks without
  requiring a dependency-injection container;
- define how navigation or view hosts create, activate, reuse, and release
  ViewModels without adding a full router in this milestone;
- add deterministic lifetime tests for unmounting, replacement, rapid
  navigation, late completion, window closure, and shutdown;
- add a Gallery page whose content loads, cancels, fails, retries, and survives
  rapid navigation without stale updates.

Exit criterion:

Asynchronous ViewModel work has an owner, resumes on the UI scheduler, cannot
outlive its view/application boundary, and can drive a standard loading or
error presentation without making composition asynchronous.

## Milestone 29 — MVVM Product Completion

Deliver:

- publish one concise architecture guide defining Model, View, ViewModel,
  Composer, state, binding, command, and task-scope responsibilities;
- publish copyable examples for synchronous commands, asynchronous commands,
  cancellation, validation, loading, retry, and error presentation;
- rename the Gallery's generic `Model` to `GalleryViewModel` so the primary
  example uses the documented language;
- add a complete Gallery workflow that combines editing, validation,
  `CanExecute`, save progress, cancellation, errors, retry, and successful
  completion;
- keep standalone, hosted, and headless Gallery products on the same View and
  ViewModel implementation;
- evaluate opt-in boilerplate reduction only after the manual typed APIs are
  stable; any generator must emit those public APIs and remain optional;
- add API comments, compatibility notes, diagnostics, and focused examples for
  every public MVVM contract;
- verify install/export consumption and ensure the MVVM layer does not pull a
  native backend into backend-neutral applications;
- publish task, subscription, and composition budgets and run the supported
  platform Gallery smoke matrix.

Exit criterion:

A new developer can build and understand a complete MVVM screen from the
public documentation, and the Gallery visibly demonstrates every supported
async and validation state without private helpers.

## Version 0.3 Definition Of Done

Version 0.3 is complete only when:

- commands expose typed availability, execution, cancellation, and errors;
- asynchronous command failures cannot disappear through unobserved detached
  work;
- computed and validation state propagate deterministically;
- ViewModel task scopes cancel on every documented lifetime boundary;
- composition remains synchronous and free from retained `Composer` objects;
- loading, empty, content, failure, retry, and cancellation are demonstrated;
- standalone, hosted, and headless Gallery paths use the same ViewModel;
- public documentation and examples match the shipped contracts;
- each milestone has targeted verification evidence and its own commit.

## Explicitly Outside Version 0.3

- XAML or another markup language;
- string-based binding paths;
- a required ViewModel base class;
- a general dependency-injection container;
- a full navigation router or URL system;
- persistence, synchronization, or networking frameworks;
- background-service lifetime beyond an owning application task scope;
- designer and hot reload;
- mandatory source generation.

These may receive separate plans after the typed MVVM contracts have proven
stable.

## Delivery Rule

Each milestone closes with:

1. focused implementation and tests;
2. matching Gallery and documentation changes;
3. one targeted verification pass;
4. roadmap/progress evidence;
5. a dedicated commit before work begins on the next milestone.
