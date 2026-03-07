# NGIN.Core Architecture

NGIN.Core implements the Spec 002 runtime-kernel contract.

## Subsystems

- Module loader/resolver
- Service registry
- Event bus
- Task runtime
- Configuration store
- Kernel orchestrator

## Loading model

- Static-first module loading is production-ready in v1.
- Dynamic plugin loading remains behind `IPluginCatalog` and `IPluginBinaryLoader` seams pending Spec 003.
- Module catalogs are per-kernel (`IModuleCatalog` / `StaticModuleCatalog`) and must be supplied explicitly by the builder, host config, or tests.
- Filesystem dynamic descriptor discovery is XML-only and scans for `.module.xml` / `.plugin-module.xml` under `pluginSearchPaths`.
- Resolver enforces:
  - descriptor family layer constraints
  - canonical load-phase ordering
  - module `compatiblePlatformRange` against host `platformVersion`
  - dependency `requiredVersion` checks

Dynamic descriptor shape:

- root element: `<Module>`
- required root attribute: `Name`
- optional root attributes:
  - `Family`
  - `Type`
  - `LoadPhase`
  - `Version`
  - `CompatiblePlatformRange`
  - `ReflectionRequired`
- supported child sections:
  - `Platforms`
  - `Dependencies`
  - `ProvidesServices`
  - `RequiresServices`
  - `Capabilities`

See [Spec 003: Package, Module, and Plugin Model](/home/berggrenmille/NGIN/docs/specs/003-plugin-abi-header-spec.md) and the example descriptor at [demo.module.xml](/home/berggrenmille/NGIN/docs/examples/runtime-descriptors/DemoPlugin/demo.module.xml).

## Service Model (DI V2)

- Service lifetimes:
  - `Singleton`
  - `Scoped`
  - `Transient`
- Explicit scopes:
  - `BeginScope(ServiceScopeKind, owner)`
  - `EndScope(ServiceScopeId)`
- Scoped providers cache per resolve-scope and are cleaned deterministically when scopes end.
- Kernel creates per-module scopes and enforces descriptor `requiresServices` after `OnRegister` and before `OnInit`.

## Events and Tasks

- Reserved kernel events are emitted on lifecycle transitions:
  - `KernelStarting`
  - `KernelRunning`
  - `KernelStopping`
  - `ModuleLoaded`
  - `ModuleStarted`
  - `ModuleFailed`
  - `ConfigChanged`
- Deferred events are queue-owned (`Main`, `IO`, `Worker`, `Background`, `Render`) with queue-specific flush APIs.
- Task runtime uses lane-specific schedulers:
  - `Main`
  - `IO`
  - `Worker`
  - `Background`
  - optional `Render`
- Barriers are lane-aware (`Barrier(lane)`) with `BarrierAll()`.

## Host Config Enforcement

`KernelHostConfig` fields are applied during startup:

- `workingDirectory` for relative-path resolution
- `configSources` layered into config store
- `commandLineArgs` (`--Key=Value`) command-line overrides
- `environmentName` environment-layer kernel key
- `pluginSearchPaths` for default filesystem plugin descriptor discovery
- `schedulerPolicy.enableRenderLane` wired to task runtime lane construction
- API threading policy:
  - `SingleThreadOnly`
  - `Serialized`

## Observability

- Uses `NGIN.Log` with categories:
  - `Kernel`
  - `ModuleLoader`
  - `Services`
  - `Events`
  - `Tasks`
  - `Config`
  - `Plugin`
