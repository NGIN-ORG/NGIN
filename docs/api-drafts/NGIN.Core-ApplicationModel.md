# NGIN.Core Application Model Draft

Status: First concrete public draft  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

## Purpose

This document defines the first concrete public draft for the `NGIN.Core` application model.

It is intentionally:

- concrete enough to guide implementation
- public-API-first
- builder-first
- package-first
- limited to draft artifacts only

It is intentionally not:

- a working runtime implementation
- a compatibility layer over legacy host naming
- a binary plugin ABI spec

## Draft Artifacts

- Draft public header: `docs/api-drafts/include/NGIN/Core/Application.hpp`
- Project manifest schema: `manifests/project.schema.json`
- Package manifest schema: `manifests/package.schema.json`
- Example application bootstrap: `docs/examples/project-model/ApplicationBuilder.Basic.cpp`
- Example project manifest: `docs/examples/project-model/ngin.project.json`
- Example package manifest: `docs/examples/project-model/ngin.package.json`

## Public API Surface

The draft header defines:

- `NGIN::Core::HostProfile`
- `NGIN::Core::TargetType`
- `NGIN::Core::ServiceLifetime`
- `NGIN::Core::ServiceScopeKind`
- `NGIN::Core::ApplicationBuilder`
- `NGIN::Core::IApplicationHost`
- `NGIN::Core::ProjectManifest`
- `NGIN::Core::PackageManifest`
- `NGIN::Core::PackageReference`
- `NGIN::Core::PluginReference`
- `NGIN::Core::TargetDefinition`
- `NGIN::Core::PackageBootstrapDescriptor`
- `NGIN::Core::PackageBootstrapContext`

It also defines the minimal builder-owned configuration surfaces needed to keep the API concrete:

- `ServiceCollection`
- `PackageCollection`
- `ModuleCollection`
- `PluginCollection`
- `ConfigurationBuilder`

## Builder Model

`ApplicationBuilder` is the primary public bootstrap API for NGIN applications.

The intended flow is:

1. create a builder
2. optionally load a project manifest
3. set or override the target selection
4. configure services, packages, modules, plugins, and config
5. build an immutable application host
6. start, run, stop, and shut down through `IApplicationHost`

Behavior rules:

- builder state is mutable until `Build()`
- host state is immutable after `Build()`
- project-manifest input is optional but first-class
- target selection happens before `Build()`
- package references are the primary composition unit
- module and plugin enable/disable lists are target-level overrides

## Service Lifetime Model

The draft now makes service lifetimes explicit in the public `NGIN.Core` API.

Supported lifetimes:

- `Singleton`: one instance per built application host
- `Scoped`: one instance per explicit scope
- `Transient`: a new instance per resolve

Supported scope kinds in the draft:

- `Host`
- `Package`
- `Module`
- `Operation`

Important rule:

- service lifetime semantics are defined by the platform, not by individual modules

That means:

- applications register services through `ApplicationBuilder::Services()`
- package bootstraps register services through the same `ServiceCollection`
- modules may choose which lifetime to use for their services, but they do not invent their own lifetime semantics

The draft `ServiceCollection` now exposes:

- `Add(ServiceRegistration)`
- `AddSingleton(...)`
- `AddScoped(...)`
- `AddTransient(...)`

The draft `IServiceProvider` now exposes explicit scope management:

- `BeginScope(...)`
- `EndScope(...)`

## Project Manifest

The shared project model for the future CLI and VS Code extension is `ngin.project.json`.

This file is target-oriented by design. It is the place where tools discover:

- project identity
- available targets
- host profile selection
- package references
- module enable/disable overrides
- plugin enable/disable overrides
- environment/config defaults

The file schema is defined in `manifests/project.schema.json`.

## Package Manifest

The reusable package model is `ngin.package.json`.

This file is intentionally minimal in the first draft. It tells tooling:

- what the package is
- which platform versions it supports
- which platforms it supports
- which package dependencies it needs
- which modules and plugins it provides
- whether it exposes a formal package bootstrap hook

The file schema is defined in `manifests/package.schema.json`.

This draft does not replace the existing detailed module/plugin catalogs. Those remain the detailed semantic source of truth during the transition.

## Package Bootstrap Convention

NGIN needs a formal equivalent to the common ".NET package adds itself to the builder" experience.

The draft package bootstrap convention is:

1. a package may declare a bootstrap entrypoint in `ngin.package.json`
2. that entrypoint configures the builder through `PackageBootstrapContext`
3. the entrypoint uses the same service/module/plugin/config surfaces as the application bootstrap

Manifest shape:

```json
"bootstrap": {
  "mode": "BuilderHookV1",
  "entryPoint": "NGIN_Bootstrap_NGIN_ECS",
  "autoApply": true
}
```

Entrypoint signature:

```cpp
extern "C" auto NGIN_Bootstrap_NGIN_ECS(
    NGIN::Core::PackageBootstrapContext& context) -> NGIN::Core::CoreResult<void>;
```

Convention rules:

- `mode` is `BuilderHookV1` in the first draft
- `entryPoint` is a unique symbol-style identifier
- `autoApply=true` means the builder may apply the package bootstrap automatically when that package is referenced
- packages may still expose friendlier convenience functions such as `AddEcs(ApplicationBuilder&)`, but those are package sugar, not the platform contract

The draft `PackageCollection` now exposes:

- `Add(...)`
- `ApplyBootstrap(packageName)`
- `ApplyBootstrap(packageName, entryPoint)`

This gives NGIN both:

- a generic toolable platform convention
- room for package-specific convenience helpers

## Example

Conceptually:

```cpp
auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);

builder->UseProjectFile("ngin.project.json");
builder->SetApplicationName("Sandbox.Game");
builder->SetDefaultTarget("Sandbox.Game");
builder->UseProfile(NGIN::Core::HostProfile::Game);

builder->Services().AddDefaults().AddLogging().AddConfiguration();
builder->Packages()
    .Add({"NGIN.ECS", ">=0.1.0 <1.0.0", false})
    .ApplyBootstrap("NGIN.ECS");
builder->Modules().Enable("Core.Hosting").Enable("Domain.ECS");

auto app = builder->Build();
```

See `docs/examples/project-model/` for the full draft examples.
