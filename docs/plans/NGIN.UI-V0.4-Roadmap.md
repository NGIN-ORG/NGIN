# NGIN.UI Version 0.4 Roadmap

Status: In progress — Milestones 30–32 complete
Theme: Application composition, dependency injection, pages, and navigation
Baseline: [`NGIN.UI-V0.3-Roadmap.md`](NGIN.UI-V0.3-Roadmap.md)
Execution map:
[`NGIN.UI-V0.4-Implementation-Workstreams.md`](NGIN.UI-V0.4-Implementation-Workstreams.md)
Progress: [`NGIN.UI-V0.4-Progress.md`](NGIN.UI-V0.4-Progress.md)
Core DI baseline:
[`NGIN-Core-DI-Implementation-Plan.md`](NGIN-Core-DI-Implementation-Plan.md)

## Purpose

Version 0.3 makes an individual screen safe and expressive, but a larger
application still has to connect NGIN.Core services to ViewModel factories,
choose pages manually, and implement navigation ownership itself. NGIN.Core
already provides singleton, scoped, and transient services. NGIN.Reflection
already describes constructors. NGIN.UI already owns ViewModel activation and
task cancellation. Version 0.4 connects those foundations into one coherent,
typed application model.

The release is ordered around ownership:

1. NGIN.Core must finish deterministic typed construction without requiring
   reflection;
2. NGIN.Reflection may supply constructor metadata when its existing Core
   feature is enabled;
3. NGIN.UI.Hosting must adapt Core service scopes to UI and ViewModel lifetime;
4. pages and navigation need explicit typed registration and predictable
   teardown;
5. the complete path must be as easy to copy as the version 0.3 MVVM screen.

## Architectural Decisions

- NGIN.Core owns service registration, resolution, lifetimes, scopes, cycle
  detection, and service diagnostics.
- NGIN.Reflection supplies optional type and constructor metadata. It is not a
  service locator and does not own service instances.
- The existing `NGIN.Core` `Reflection` feature remains opt-in. Core, NGIN.UI,
  standalone applications, and explicit factories must continue to work when
  reflection is absent.
- NGIN.UI remains independent of NGIN.Core. The Core-to-UI adapter belongs in
  `NGIN.UI.Hosting`.
- The page catalogue and navigation engine remain backend-neutral NGIN.UI
  contracts. Hosted applications supply a Core-backed activator; standalone
  applications supply explicit factories.
- A View owns controls and layout through a synchronous composition function.
  Pages do not move composition into a ViewModel or service container.
- ViewModels are resolved from a page scope. Leaving a page cancels its task
  scope before its scoped services are released.
- Page registration is explicit and typed. Reflection may reduce constructor
  boilerplate, but does not silently discover or register every reflected
  type.
- Manual registration and factories are the canonical public contract. Any
  MetaGen assistance emits those same registrations and remains optional.
- Navigation is per window or named region. There is no process-global current
  page or service locator.

## Current Baseline And Gaps

| Area | Current capability | Version 0.4 target |
|---|---|---|
| Service lifetimes | Singleton, scoped, transient | Preserve and diagnose |
| Automatic construction | `T()` or `T(Shared<IServiceProvider>)` | Typed dependencies and optional reflected constructors |
| Explicit factories | Supported | Remain the final escape hatch |
| Reflection | Constructor metadata and invocation | Safe DI activator bridge |
| UI service access | Narrow manual `ViewModelServiceResolver` | Hosted Core provider adapter |
| ViewModel ownership | `KeyedViewModelHost<T>` | Page-scoped activation through DI |
| Pages | Application-authored enums and switches | Typed explicit page catalogue |
| Navigation | Application-owned | Push, replace, back, and regions |
| Standalone apps | Explicit factories | Keep the same backend-neutral path |
| Developer setup | Several manual connections | One documented application composition path |

## Milestone 30 — Complete Typed Core DI

Deliver:

- add a public compile-time dependency declaration such as
  `ServiceDependencies<T...>` for ordinary constructor injection without
  reflection;
- make `AddSingleton<T>()`, `AddScoped<T>()`, and `AddTransient<T>()` select
  construction in a documented deterministic order;
- add service-to-implementation registration overloads, including interface
  contracts, without requiring application-written cast factories;
- preserve explicit instance and factory registration as the highest-control
  path;
- resolve every dependency in the current construction scope and reject a
  singleton that captures an incompatible scoped dependency;
- detect constructor dependency cycles and report the complete service chain;
- reject duplicate or ambiguous unnamed and named registrations with stable
  diagnostics;
- expose read-only service graph, lifetime, scope, activation-count, and
  failure diagnostics;
- cover lazy construction, interface mappings, named services, all lifetimes,
  nested scopes, missing dependencies, cycles, invalid lifetime capture,
  factory failure, and concurrent resolution;
- update the Core DI guide so reflection-free applications have one complete
  copyable path.

Construction precedence for this milestone:

1. an explicitly supplied instance or factory;
2. `T::Dependencies`, when declared;
3. `T(Shared<IServiceProvider>)`;
4. `T()`.

Exit criterion:

A hosted application can register interfaces, implementations, and ordinary
constructor dependencies with correct lifetime behavior and useful error
chains, without enabling NGIN.Reflection.

## Milestone 31 — Reflection-Backed Constructor Injection

Deliver:

- activate the existing optional `NGIN.Core` `Reflection` feature as the DI
  metadata bridge without adding a mandatory dependency;
- define an explicit injectable-constructor marker or equivalent unambiguous
  rule for `TypeBuilder<T>::Constructor<...>()` metadata;
- support `NGIN::Memory::Shared<T>` constructor parameters first, preserving
  the dependency scope and ownership contract from Milestone 30;
- define typed metadata for optional and named dependencies without encoding
  service behavior in free-form strings;
- validate reflected constructors when a service is registered, so ambiguity,
  unsupported parameter kinds, missing metadata, and inaccessible invokers
  fail before first use;
- build or cache a typed activation plan rather than repeatedly interpreting
  metadata on every resolve;
- prove instance ownership is safe across reflection module and library
  boundaries before accepting constructor invocation as a DI path;
- invalidate cached activation plans safely when reflection modules unload or
  their generation changes;
- make handwritten reflection and MetaGen-produced metadata behave
  identically;
- preserve the reflection-free construction order and explicit factories;
- add cross-module tests for success, missing services, ambiguity, named
  dependencies, scopes, unload, stale metadata, and ABI-safe destruction.

Construction precedence after this milestone:

1. an explicitly supplied instance or factory;
2. `T::Dependencies`, when declared;
3. one explicitly injectable reflected constructor, when the feature is on;
4. `T(Shared<IServiceProvider>)`;
5. `T()`.

Exit criterion:

`AddTransient<T>()` can construct a reflected type from scoped shared
dependencies without application-written factories, while the identical Core
and UI build still works with Reflection disabled.

## Milestone 32 — Hosted UI Service And Scope Integration

Deliver:

- add the Core service-provider adapter in `NGIN.UI.Hosting`, leaving
  `Packages/NGIN.UI` free of Core and Reflection dependencies;
- define application, window, page, and transient activation scope ownership;
- create one window scope per hosted UI window and one child page scope per
  mounted page;
- resolve ViewModels through Core and retain their service dependencies for the
  complete mounted lifetime;
- settle one safe ownership bridge between `NGIN::Memory::Shared<T>` and the
  ViewModel host instead of exposing borrowed raw pointers as the primary DI
  path;
- adapt or supersede `ViewModelServiceResolver` without breaking standalone
  explicit factories unnecessarily;
- guarantee teardown order: block new work, deactivate the ViewModel, cancel
  and observe its task scope, release the ViewModel, then end the DI scope;
- route service resolution and activation errors into structured UI startup or
  navigation errors;
- keep standalone and headless applications able to supply explicit factories
  without NGIN.Core;
- test window closure, rapid replacement, task cancellation, scoped-service
  destruction, application shutdown, and resolution failures.

Exit criterion:

A hosted page can request a DI-created ViewModel whose scoped services and
asynchronous work share one deterministic lifetime, while NGIN.UI core remains
backend- and host-neutral.

## Milestone 33 — Typed Pages And Navigation

Deliver:

- define a typed `PageRegistry` contract that explicitly associates a stable
  page identity, ViewModel type, synchronous View composition function, and
  optional display/deep-link name;
- make duplicate identities, missing ViewModels, incompatible composition
  functions, and route-name conflicts registration errors;
- add an application-builder extension in `NGIN.UI.Hosting` for service and
  page registration without adding pages to NGIN.Core itself;
- add a per-window or named-region `NavigationService` and composition host;
- support typed startup page, navigate/push, replace, back, and clear-stack
  operations;
- define typed navigation parameters without string property bags;
- create and end a page DI scope for each stack entry and connect it to
  ViewModel activation, deactivation, and cancellation;
- preserve keyed identity, focus, semantics, scroll position, and retained
  state for entries that remain on the stack;
- make page caching explicit and bounded rather than an accidental consequence
  of retained controls;
- serialize navigation mutations on the UI scheduler and reject reentrant or
  conflicting operations with observable results;
- define navigation failure behavior without leaving a half-mounted page;
- cover keyboard back, window-local stacks, multiple windows, regions, rapid
  navigation, failure rollback, focus restoration, and headless determinism.

Exit criterion:

An application can explicitly register pages and navigate between them through
typed APIs; each page receives a correctly scoped ViewModel and leaving it
reliably tears down its UI work and services.

## Milestone 34 — Application Composition Product Completion

Deliver:

- publish one concise application-composition guide covering Core DI,
  reflection-free and reflection-backed construction, hosted scopes, pages,
  navigation, standalone factories, and test substitution;
- provide a small buildable multi-page application with services, a reflected
  ViewModel, navigation parameters, back navigation, an async load, and
  deterministic teardown;
- migrate the Gallery from its page enum/switch ownership to the public page
  registration and navigation contracts while preserving standalone, hosted,
  and headless products;
- add a Gallery diagnostics view for the current page stack, page scopes,
  ViewModel tasks, service activations, and resolution failures;
- add testing helpers for service overrides, initial page selection,
  navigation assertions, and scope-leak checks without a native backend;
- evaluate optional MetaGen registration output only after the manual APIs are
  stable; generated code must remain readable, deterministic, and removable;
- publish migration and source-compatibility notes for any version 0.3
  ViewModel-host API changes;
- verify public install/export consumption with Reflection both disabled and
  enabled;
- run Core DI, Reflection ABI, UI lifetime, standalone, hosted, headless, and
  Windows/Linux/macOS Gallery release gates;
- version the NGIN.UI package family, Gallery, and documentation consistently
  as 0.4; NGIN.Core and NGIN.Reflection retain their independently governed
  package versions.

Exit criterion:

A new developer can register services and pages, constructor-inject a
ViewModel, navigate, go back, test the flow headlessly, and understand every
lifetime boundary from one public buildable example.

## Version 0.4 Definition Of Done

Version 0.4 is complete only when:

- reflection-free typed constructor injection and explicit factories both
  remain first-class;
- reflected constructor injection is opt-in, deterministic, cached, scoped,
  and safe across module boundaries;
- Core remains the only owner of service lifetimes and scopes;
- NGIN.UI core has no Core or Reflection dependency;
- hosted window and page scopes release every scoped service exactly once;
- ViewModel tasks are canceled before their service scope ends;
- pages are registered explicitly and Views still own controls and layout;
- navigation is typed, window-local, rollback-safe, and headless-testable;
- standalone, hosted, and headless applications can use the same View and
  ViewModel implementations;
- public documentation and examples match the shipped APIs;
- every milestone has targeted verification evidence and its own commit.

## Explicitly Outside Version 0.4

- XAML or another markup language;
- string-based property binding or reflection-driven binding paths;
- property or field injection;
- a process-global service locator or current page;
- mandatory Reflection or MetaGen use;
- automatic registration of every reflected type;
- arbitrary constructor parameter conversion through `Any`;
- URL parsing, browser history, web routing, or universal/deep-link policy;
- persistence or restoration of navigation stacks across process restarts;
- a visual designer or hot reload;
- application networking, storage, or synchronization frameworks.

## Delivery Rule

Each milestone closes with:

1. an approved public contract and ownership rules;
2. focused implementation and tests;
3. a public example or Gallery demonstration;
4. one targeted verification pass and recorded evidence;
5. a dedicated commit before work begins on the next milestone.
