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
- Example package registrar/bootstrap: `docs/examples/project-model/PackageBootstrap.ECS.cpp`

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
- `NGIN::Core::PackageBootstrapEntry`
- `NGIN::Core::PackageBootstrapRegistry`
- `NGIN::Core::PackageBootstrapContext`
- `NGIN::Core::PackageBootstrapRegistrarFn`

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
5. register linked package registrars
6. build an immutable application host
7. start, run, stop, and shut down through `IApplicationHost`

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

The first concrete implementation path is a static-link, explicit-registrar design.

The draft package bootstrap convention is:

1. a package may declare a bootstrap entrypoint in `ngin.package.json`
2. the package exposes a linked registrar function that registers bootstrap entries into `PackageBootstrapRegistry`
3. the entrypoint configures the builder through `PackageBootstrapContext`
4. the entrypoint uses the same service/module/plugin/config surfaces as the application bootstrap

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

Registrar signature:

```cpp
extern "C" void NGIN_RegisterPackage_NGIN_ECS(
    NGIN::Core::PackageBootstrapRegistry& registry);
```

Convention rules:

- `mode` is `BuilderHookV1` in the first draft
- `entryPoint` is a unique symbol-style identifier
- linked package registrars are supplied explicitly through the builder
- `autoApply=true` means the builder may apply the package bootstrap automatically when that package is directly referenced by the selected target
- v1 assumes one default bootstrap hook per package; `FindDefault(packageName)` returns that package-level default hook
- packages may still expose friendlier convenience functions such as `AddEcs(ApplicationBuilder&)`, but those are package sugar, not the platform contract
- no static global auto-registration is part of the v1 contract
- no dynamic symbol discovery is part of the v1 contract

The draft `PackageBootstrapRegistry` now exposes:

- `Register(PackageBootstrapEntry)`
- `Find(packageName, entryPoint)`
- `FindDefault(packageName)`

The draft `PackageCollection` now exposes:

- `Add(...)`
- `RegisterLinkedRegistrar(...)`
- `ApplyBootstrap(packageName)`
- `ApplyBootstrap(packageName, entryPoint)`

This gives NGIN both:

- a generic toolable platform convention
- room for package-specific convenience helpers

## Builder Invocation Rules

The first implementation should use the following builder-time rules:

1. load the selected project target
2. collect direct package references from that target
3. execute all linked package registrars into a builder-owned `PackageBootstrapRegistry`
4. determine which direct packages should bootstrap:
   - explicit `ApplyBootstrap(...)`
   - plus direct referenced packages whose manifest has `bootstrap.autoApply=true`
5. order bootstrap execution by package dependency order within the direct referenced package set
6. use target manifest order as the tiebreak for independent packages
7. invoke bootstrap hooks
8. freeze builder state and continue to host build

Failure rules:

- required package with declared bootstrap metadata but no matching registered hook: hard error
- required package bootstrap hook returns error: hard error
- optional package bootstrap missing or failing: warning and skip only if the package reference itself is optional
- explicit `ApplyBootstrap(...)` upgrades missing or failing bootstrap to a hard error

## Authoring Model

The first authoring model should be explicit and portable.

Package side:

- define bootstrap function
- define one explicit registrar function
- registrar registers package name, entrypoint, and function pointer

Application side:

- explicitly reference linked package registrar symbols
- register them through `Packages().RegisterLinkedRegistrar(...)`
- this explicit symbol reference prevents static-library dead stripping in v1

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
    .RegisterLinkedRegistrar(&NGIN_RegisterPackage_NGIN_ECS)
    .Add({"NGIN.ECS", ">=0.1.0 <1.0.0", false})
    .ApplyBootstrap("NGIN.ECS");
builder->Modules().Enable("Core.Hosting").Enable("Domain.ECS");

auto app = builder->Build();
```

See `docs/examples/project-model/` for the full draft examples.
