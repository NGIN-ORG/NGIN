# NGIN.UI Version 0.4 Implementation Workstreams

Status: In progress — Workstreams A–C complete
Roadmap: [`NGIN.UI-V0.4-Roadmap.md`](NGIN.UI-V0.4-Roadmap.md)

## Purpose

This document turns the version 0.4 application-composition roadmap into
bounded ownership workstreams. Public names and the directional examples below
remain provisional until their milestone contract is approved.

## Ownership Boundaries

| Concern | Primary ownership |
|---|---|
| Service keys, registrations, lifetimes, scopes, resolution, and diagnostics | `Packages/NGIN.Core/` |
| Type, constructor, parameter, module-generation, and invocation metadata | `Dependencies/NGIN/NGIN.Reflection/` |
| Optional Core/Reflection feature wiring | `Packages/NGIN.Core/` and its package feature |
| Backend-neutral ViewModel, page catalogue, navigation, and page-host contracts | `Packages/NGIN.UI/` |
| Core provider adapter, Core-backed window/page scopes, and app-builder extensions | `Packages/NGIN.UI.Hosting/` |
| Optional registration code generation | `Packages/NGIN.Reflection.MetaGen/` |
| Shared demonstrations | `Examples/NGIN.UI.Gallery/` and a focused multi-page example |
| Deterministic product coverage | `Examples/NGIN.UI.Gallery.Tests/` |
| Native hosted coverage | `Examples/NGIN.UI.Gallery.Hosted/` |
| Guides, compatibility, and plans | `docs/guides/`, `docs/policies/`, and `docs/plans/` |

NGIN.UI core must not include Core service types. NGIN.Reflection must not
create or retain service scopes. NGIN.Core must not know about pages, Views,
windows, or navigation. Standalone applications must be able to use the same
page catalogue and navigation engine with explicit factories.

## Directional Developer Experience

The intended amount of application code is approximately:

```cpp
auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);

builder->Services()
    .AddSingleton<ISettings, Settings>()
    .AddScoped<IDocumentSession, DocumentSession>()
    .AddTransient<HomeViewModel>()
    .AddTransient<EditorViewModel>();

auto ui = NGIN::UI::Hosting::ConfigureUIHosting(*builder, hostingInfo);
ui.Pages()
    .Add<HomePage, HomeViewModel>(ComposeHomePage)
    .Add<EditorPage, EditorViewModel>(ComposeEditorPage)
    .SetStartup<HomePage>();
```

Navigation should be typed:

```cpp
navigation.Navigate<EditorPage>(EditorParameters{documentId});
navigation.Back();
```

This is a target experience, not an approved signature. The contract phase
must settle error types, ownership, registration timing, and type-erasure
boundaries before these names are implemented.

## Workstream A — Core Construction And Service Graph

### Goal

Finish reflection-free DI so the optional Reflection path enhances ergonomics
instead of becoming a correctness dependency.

### Contract Decisions

- `ServiceDependencies<T...>` declaration and constructor matching;
- interface-to-implementation registration and ownership conversion;
- explicit factory precedence;
- named and optional dependencies;
- lifetime compatibility rules;
- cycle-detection chain and failure caching;
- registration freeze and thread-safety boundary;
- diagnostics snapshot shape and sensitive-value policy.

### Verification

- every lifetime and scope combination;
- lazy singleton contention and transient construction;
- interface mappings and named services;
- dependency chains, missing dependencies, cycles, and lifetime violations;
- failure rollback and repeated resolution;
- zero changes to applications using explicit factories.

## Workstream B — Reflection DI Activators

### Goal

Translate constructor metadata into the same internal typed factory contract
used by Workstream A.

### Contract Decisions

- injectable-constructor marker and ambiguity rules;
- supported parameter metadata, initially `Shared<T>`;
- optional and named dependency representation;
- conversion from reflected instance ownership to the registered service type;
- activation-plan cache identity and module-generation invalidation;
- registration-time versus resolution-time validation;
- cross-library ABI and destruction ownership;
- behavior when the feature or metadata is absent.

### Required Spike

Before public API approval, prove one complete reflected construction across a
dynamic module boundary, including shared dependency resolution, returned
instance ownership, module unload rejection while live instances exist, and
destruction by the correct allocator/module. If runtime invocation cannot meet
that contract cleanly, MetaGen must emit a typed factory that consumes the same
constructor metadata; the public DI behavior must remain unchanged.

### Verification

- handwritten and generated metadata parity;
- one, missing, ambiguous, and unsupported constructors;
- named, optional, singleton, scoped, and transient dependencies;
- module reload, stale handles, cache invalidation, and destruction;
- Reflection-off build and install consumer.

## Workstream C — Hosted Scope And ViewModel Activation Bridge

### Goal

Give hosted ViewModels Core services without putting Core into NGIN.UI.

### Contract Decisions

- provider/activator abstraction visible to NGIN.UI core, if one is needed;
- implementation location in NGIN.UI.Hosting;
- `NGIN::Memory::Shared<T>` and `std::shared_ptr<T>` ownership strategy;
- application, window, page, popup, and transient activation scopes;
- exact activation, cancellation, deactivation, and scope-end ordering;
- treatment of ViewModel factories in standalone applications;
- `ViewModelServiceResolver` compatibility or deprecation path;
- UI-thread scheduling and structured error propagation.

### Verification

- ViewModel resolution with singleton and page-scoped dependencies;
- same-window replacement and multiple-window isolation;
- async activation, navigation during load, window close, and shutdown;
- service destructor order and exactly-once scope release;
- missing service and constructor failure presentation;
- standalone/headless operation without NGIN.Core.

## Workstream D — Page Catalogue

### Goal

Associate page identity, ViewModel construction, and synchronous View
composition through an explicit typed catalogue.

### Contract Decisions

- page tag versus page class representation;
- stable `PageId` and optional external route name;
- typed composition function signature;
- typed navigation parameter ownership and validation;
- registration freeze, duplicate behavior, and module contributions;
- page metadata available to menus, accessibility, and diagnostics;
- whether page descriptors are static data or singleton services;
- test override and replacement rules.

### Verification

- registration and lookup by type and stable identity;
- duplicate, missing, mismatched, and unloaded registrations;
- composition with the resolved ViewModel type;
- module-contributed pages without global initialization ordering;
- standalone manual catalogue and hosted builder extension parity.

## Workstream E — Navigation And Page Lifetime

### Goal

Own page stacks and scopes without turning composition asynchronous.

### Contract Decisions

- navigation result and failure representation;
- push, replace, back, clear, and startup semantics;
- one stack per window or named region;
- page-entry keys, caching, and retained-state policy;
- focus and accessibility restoration;
- reentrant navigation and UI-scheduler serialization;
- failure rollback and visibility during async activation;
- close/back interception boundaries without adding unsaved-data policy to the
  framework;
- task cancellation and DI scope teardown sequence.

### Verification

- startup, push, replace, back, and empty-stack behavior;
- typed parameters and invalid requests;
- multiple windows and independent regions;
- rapid and reentrant navigation;
- activation failure rollback and late task completion;
- focus, semantics, retained state, and bounded page cache;
- no leaked tasks, subscriptions, scopes, or ViewModels after teardown.

## Workstream F — Authoring, Tooling, And Product Evidence

### Goal

Make the complete service-to-page path easy to learn, inspect, and test.

### Deliverables

- application-composition and migration guides;
- reflection-free and reflection-enabled install consumers;
- focused buildable multi-page example;
- Gallery migration and service/navigation diagnostics;
- headless page and navigation testing helpers;
- optional MetaGen registration experiment after manual APIs stabilize;
- public API comments and compatibility notes;
- Windows, Linux, and macOS release matrix.

## Milestone Mapping

| Milestone | Workstreams |
|---|---|
| 30 — Complete Typed Core DI | A |
| 31 — Reflection-Backed Constructor Injection | B |
| 32 — Hosted UI Service And Scope Integration | C |
| 33 — Typed Pages And Navigation | D and E |
| 34 — Application Composition Product Completion | F and final integration |

## Execution Order

### Wave 1 — Reflection-Free Correctness

1. Approve service dependency, mapping, lifetime, and diagnostic contracts.
2. Implement Workstream A.
3. Run focused Core service tests and install consumption.
4. Close and commit Milestone 30.

### Wave 2 — Optional Reflection Ergonomics

1. Complete the cross-module ownership spike.
2. Approve constructor marker, parameter, cache, and unload rules.
3. Implement Workstream B without changing Reflection-off behavior.
4. Run Reflection ABI and Core DI verification.
5. Close and commit Milestone 31.

### Wave 3 — Hosted Lifetime Bridge

1. Approve ViewModel ownership and scope teardown ordering.
2. Implement Workstream C.
3. Add hosted and headless service-lifetime demonstrations.
4. Close and commit Milestone 32.

### Wave 4 — Pages And Navigation

1. Approve page identity, composition, parameters, stack, and region contracts.
2. Implement Workstreams D and E.
3. Add deterministic navigation and failure-rollback coverage.
4. Close and commit Milestone 33.

### Wave 5 — Product Completion

1. Complete Workstream F and migrate the Gallery.
2. Evaluate optional MetaGen assistance against the manual API.
3. Run install, package, ABI, Gallery, and supported-platform gates.
4. Close and commit Milestone 34.

Milestones must remain separate commits. Do not begin a later wave with an
uncommitted completed milestone.

## Verification Matrix

| Change area | Minimum verification |
|---|---|
| Core typed DI | lifetime, scope, graph, cycle, mapping, contention tests |
| Reflection activation | ABI, ownership, metadata, cache, unload tests |
| Hosted adapter | window/page scope and ViewModel teardown tests |
| Page catalogue | typed registration, conflict, module, composition tests |
| Navigation | stack, region, parameters, rollback, focus, headless tests |
| Standalone parity | explicit-factory build with no Core dependency |
| Public packages | Reflection-off and Reflection-on install consumers |
| Product completion | standalone, hosted, headless, and platform Gallery matrix |

## Milestone Closure Checklist

- public contract and ownership rules are written before implementation;
- the change stays inside the ownership table above;
- Reflection remains optional and explicit factories remain supported;
- every new scope has a documented creator, owner, cancellation boundary, and
  end condition;
- Views remain synchronous and own controls and layout;
- focused tests cover success, failure, cancellation, teardown, and races;
- public examples use only installed APIs;
- generated build output is not edited;
- targeted verification and evidence are recorded;
- the milestone is committed before the next milestone begins.
