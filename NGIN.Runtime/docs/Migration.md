# NGIN.Runtime Migration (Breaking Pass)

This document summarizes the API-breaking hardening changes introduced in the Spec 001 + Spec 002 contract pass.

## Service Registry

- Replaced implicit service caching with explicit lifetimes:
  - `ServiceLifetime::Singleton`
  - `ServiceLifetime::Scoped`
  - `ServiceLifetime::Transient`
- Added explicit scope lifecycle:
  - `BeginScope(ServiceScopeKind, owner)`
  - `EndScope(ServiceScopeId)`
- `RegisterFactory` now takes `ServiceRegistrationOptions` (lifetime + owner scope + metadata).

## Module Context

- `ModuleContext` now exposes `ModuleScope()`.
- Module helpers now register scoped providers through:
  - `RegisterSingleton(...)`
  - `RegisterFactory(..., ServiceLifetime, ...)`

## Loader and Catalogs

- Introduced per-kernel module catalog:
  - `IModuleCatalog`
  - `StaticModuleCatalog`
- Legacy global functions (`RegisterStaticModule`, `ClearStaticModules`, `GetStaticModules`) are deprecated compatibility adapters.

## Host Config

- Added and enforced:
  - `platformVersion`
  - `apiThreadPolicy`
  - `configureServices(IServiceRegistry&)`
  - `moduleCatalog`
- Existing fields (`workingDirectory`, `configSources`, `pluginSearchPaths`, `commandLineArgs`, `environmentName`, `schedulerPolicy.enableRenderLane`) are now applied during startup.

## Events

- Added reserved event enum:
  - `KernelStarting`, `KernelRunning`, `KernelStopping`
  - `ModuleLoaded`, `ModuleStarted`, `ModuleFailed`
  - `ConfigChanged`
- Added deferred queue ownership:
  - `EventQueue::{Main, IO, Worker, Background, Render}`
  - `EnqueueDeferredTo(...)`
  - `FlushDeferredFrom(...)`

## Tasks

- Task runtime now uses lane-specific schedulers.
- `Barrier()` API changed to lane-aware form:
  - `Barrier(TaskLane lane)`
  - `BarrierAll()`
- Added lane capability query:
  - `IsLaneEnabled(TaskLane lane)`

## Resolver Contract

- Resolver now enforces descriptor `family` directly (no name-prefix inference).
- Added enforcement for:
  - `compatiblePlatformRange`
  - dependency `requiredVersion`
  - phase ordering
  - service contract preflight warnings + runtime required-service checks

