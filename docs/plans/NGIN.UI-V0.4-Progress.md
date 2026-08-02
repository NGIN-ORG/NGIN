# NGIN.UI Version 0.4 Progress

Roadmap: [`NGIN.UI-V0.4-Roadmap.md`](NGIN.UI-V0.4-Roadmap.md)
Execution map:
[`NGIN.UI-V0.4-Implementation-Workstreams.md`](NGIN.UI-V0.4-Implementation-Workstreams.md)

## Baseline

Status: In progress
Established: 2026-08-02

Existing foundations:

- NGIN.Core provides typed singleton, scoped, and transient services, named
  registrations, explicit factories, lazy construction, scopes, and typed
  required/optional resolution;
- automatic Core construction currently supports `T()` and
  `T(Shared<IServiceProvider>)`;
- NGIN.Reflection provides constructor and parameter metadata plus invocation
  and module-generation tracking;
- the `NGIN.Core` package already declares an optional `Reflection` feature;
- NGIN.UI 0.3 provides typed MVVM state, commands, validation, task scopes,
  async presentation, `ViewModelFactory<T>`, and `KeyedViewModelHost<T>`;
- NGIN.UI.Hosting publishes the hosted UI runtime, dispatcher, platform,
  renderer, and Core-backed UI scope provider;
- page registration and navigation are not yet implemented.

## Milestone 30 — Complete Typed Core DI

Status: Complete (2026-08-02)

Delivered:

- added reflection-free `ServiceDependencies<T...>` constructor injection with
  deterministic left-to-right dependency resolution;
- added singleton, scoped, and transient service-to-implementation overloads
  to the registry, application service collection, and module context;
- preserved explicit instances and factories as the highest-precedence path;
- added complete missing-dependency and cycle paths plus singleton-to-scoped
  lifetime validation;
- made concurrent singleton and scoped activation construct once per cache;
- added read-only registration, dependency, scope, activation, failure, and
  cache diagnostics;
- documented the complete reflection-free path in
  [`../guides/ngin-core-di.md`](../guides/ngin-core-di.md);
- expanded the focused Core service coverage and passed all 45 NGIN.Core tests.

## Milestone 31 — Reflection-Backed Constructor Injection

Status: Complete (2026-08-02)

Delivered:

- activated the existing optional Core `Reflection` feature in direct CMake
  and installed/package-feature consumption without changing Reflection-off
  builds;
- added one explicit `InjectableConstructor<...>()` marker with typed required,
  named, optional, and named-optional parameter bindings;
- added MetaGen `NGIN_INJECT` and `NGIN_DEPENDENCY(...)` authoring that emits
  the same public `TypeBuilder` metadata as handwritten reflection;
- validated reflected plans as providers enter the registry, cached their
  constructor/binding plans, and rebuilt them after Reflection generation
  changes;
- limited the first supported parameter contract to
  `NGIN::Memory::Shared<T>` and preserved the active Core service scope;
- added ABI-owned `Shared` aliases so reflected service destruction remains in
  the module that created the object;
- made Reflection reject module unload while live reflected instances remain;
- verified named and optional dependencies, interface mappings, missing and
  ambiguous metadata, stale-plan rebuilds, feature-off parity, and a real
  imported DLL constructor invocation;
- passed 7 focused Base smart-pointer tests, all 5 Reflection tests, all 47
  Reflection-enabled Core tests, all 45 Reflection-disabled Core tests, and
  focused CLI coverage for package-feature build-option parsing and ordering.

## Milestone 32 — Hosted UI Service And Scope Integration

Status: Complete (2026-08-02)

Delivered:

- added `HostedUIServiceProvider` in `NGIN.UI.Hosting` while keeping NGIN.UI
  independent of Core and Reflection;
- defined Core application, window, page, and activation scope kinds and
  created their hosted ownership hierarchy;
- added hosted window creation, page/activation scope handles, native-close
  reconciliation, and shutdown blocking for new work;
- added `HostedViewModelHost<T>` as the owning Core DI path while preserving
  `KeyedViewModelHost<T>` and explicit standalone factories;
- resolved ViewModels as `NGIN::Memory::Shared<T>` and added safe aliasing
  bridges to and from `std::shared_ptr<T>`;
- added drain-aware ViewModel task-scope closure and guaranteed teardown order:
  deactivate, cancel and observe active work, observe optional async
  deactivation, release the ViewModel, then end its DI scope;
- retained structured Core resolution and scope failures for startup and the
  upcoming navigation layer;
- verified page-scope reuse, multiple-window isolation, rapid replacement,
  window closure, application shutdown, async cancellation, destruction order,
  missing services, activation failures, scope diagnostics, and ownership
  bridges;
- passed all 9 focused ViewModel cases (67 assertions) and the hosted lifecycle
  executable in a clean Visual Studio Release build.

## Milestone 33 — Typed Pages And Navigation

Status: Planned

## Milestone 34 — Application Composition Product Completion

Status: Planned
