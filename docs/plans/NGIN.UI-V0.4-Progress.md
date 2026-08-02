# NGIN.UI Version 0.4 Progress

Roadmap: [`NGIN.UI-V0.4-Roadmap.md`](NGIN.UI-V0.4-Roadmap.md)
Execution map:
[`NGIN.UI-V0.4-Implementation-Workstreams.md`](NGIN.UI-V0.4-Implementation-Workstreams.md)

## Baseline

Status: Planned
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

Status: Planned

## Milestone 31 — Reflection-Backed Constructor Injection

Status: Planned

## Milestone 32 — Hosted UI Service And Scope Integration

Status: Planned

## Milestone 33 — Typed Pages And Navigation

Status: Planned

## Milestone 34 — Application Composition Product Completion

Status: Planned
