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
- NGIN.UI.Hosting publishes the hosted UI runtime, dispatcher, platform, and
  renderer through Core services;
- page registration, Core-backed ViewModel activation, and navigation are not
  yet implemented.

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

Status: Planned

## Milestone 32 — Hosted UI Service And Scope Integration

Status: Planned

## Milestone 33 — Typed Pages And Navigation

Status: Planned

## Milestone 34 — Application Composition Product Completion

Status: Planned
