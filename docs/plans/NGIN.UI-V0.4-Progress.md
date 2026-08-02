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

Status: Complete (2026-08-03)

Delivered:

- added the backend-neutral `PageRegistry` with explicit stable identities,
  optional display/route names, page-tag identity, ViewModel type, parameter
  type, factory, and synchronous View composition;
- made empty identities/factories, duplicate identities/tags/routes, and
  registration after catalogue freeze structured registration failures, while
  incompatible factory and composition signatures fail at compile time;
- added typed startup, push, replace, back, clear, keyboard-back, snapshots,
  observers, and failure observers through a window-local or named-region
  `NavigationService`;
- activated replacements before mutating the stack so missing services and
  factory failures leave the mounted page unchanged;
- retained every live stack entry under a stable composition key, including
  collapsed entries, so keyed controls, focus, semantics, scrolling, and local
  retained state survive back navigation;
- added explicit cache keys and a bounded opt-in removed-page cache; the
  default cache capacity is zero and eviction deterministically closes the
  page;
- enforced an optional UI-scheduler boundary and rejected reentrant/conflicting
  synchronous mutations with observable structured errors;
- added `NavigationHost` as the small window content/input adapter;
- added `HostedPageBuilder`, `HostedNavigationContext`, and builder extensions
  that map each entry to a Core page scope and `HostedViewModelHost<T>` without
  adding Core or Reflection dependencies to NGIN.UI;
- verified registration conflicts, typed parameters, stable retained
  composition, startup/push/replace/back/clear, cache reuse/eviction, keyboard
  back, scheduler enforcement, reentrancy, rollback, region isolation, hosted
  resolution failures, per-entry Core scopes, and scoped-service teardown;
- passed all 5 focused navigation cases (77 assertions) and the expanded hosted
  lifecycle executable in a Visual Studio Release build.

## Milestone 34 — Application Composition Product Completion

Status: Complete (2026-08-03)

Delivered:

- published the concise application-composition guide covering Core DI,
  optional reflected construction, hosted ownership, typed pages and
  navigation, standalone factories, headless substitution, diagnostics, and
  shutdown ordering;
- added the buildable `NGIN.UI.MultiPage` application with a reflection-free
  Home ViewModel, reflected Detail ViewModel, singleton and scoped services,
  a typed parameter, Back, owned async loading, and presentation-module
  teardown;
- migrated all 14 Gallery pages from composition-switch ownership to the
  public `PageRegistry`, `NavigationService`, and `NavigationHost` path while
  retaining the page enum only as a CLI/menu compatibility adapter;
- expanded Gallery diagnostics with the active region stack, page
  scope/lease counts, active and cleanup task counts, activations, releases,
  and navigation failures;
- added `PageTestContext` and `NavigationTestDriver` for typed service
  overrides, initial-page selection, ordered stack assertions, and scope-leak
  checks without a native backend;
- kept MetaGen optional and limited to readable injectable-constructor
  metadata; page identity, composition, parameters, naming, and caching remain
  explicit application code;
- published 0.3-to-0.4 migration, source-compatibility, and versioned release
  notes;
- completed `NGIN.UI.Hosting` install/export metadata, preserved its installed
  `NGIN::UI::Hosting` target name, and made the NGIN.UI install carry its
  bundled FreeType and HarfBuzz static libraries;
- verified fresh installed consumers with Core Reflection disabled and
  enabled;
- updated the existing Windows, Linux, and macOS CI matrix to build and smoke
  the 0.4 standalone, hosted, headless, and multi-page products;
- versioned NGIN.UI, Hosting, SDL3 backend, Windows accessibility, Gallery,
  examples, documentation, package ranges, release outputs, and artifacts as
  0.4 while retaining the independent Core and Reflection 0.1 versions.

Verification evidence:

- NGIN.UI navigation: 87 assertions in 6 focused cases;
- NGIN.Core Reflection off/on: 589 assertions in 45 cases and 614 assertions
  in 47 cases;
- NGIN.UI.Hosting Reflection off/on lifecycle executables: passed;
- installed Hosting consumer with Reflection off/on: configured, built, and
  ran successfully;
- `NGIN.UI.MultiPage --smoke`: passed through reflected page activation and
  deterministic shutdown;
- Gallery headless checks plus standalone and hosted native smoke runs:
  passed on Windows;
- public API documentation: 381 public types documented;
- Linux and macOS release builds and native smoke runs remain enforced by the
  repository's `ui-ci.yml` matrix.
