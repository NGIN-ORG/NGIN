# Spec 002: Application Host and Builder Model

Status: Active normative spec  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

Depends on:

- `001-module-dependency-graph.md`

Related:

- `003-plugin-abi-header-spec.md`
- `004-editor-architecture.md`
- `../api-drafts/NGIN.Core-ApplicationModel.md`

## Summary

NGIN applications should start from a common host model.

Every application type, whether editor, game, CLI tool, or service, should follow the same conceptual flow:

1. create an application builder
2. configure environment and profile
3. register services
4. add modules and packages
5. discover or load plugins
6. build the host
7. start, run, stop, and shut down predictably

This spec defines that hosted application model.

## Naming Direction

The current implementation lives in `NGIN.Core`.

The approved platform direction is to treat that implementation as the central hosting/core layer of NGIN and move the product language toward `NGIN.Core`.

For this spec:

- **current implementation name**: `NGIN.Core`
- **target platform name**: `NGIN.Core`

The first concrete public draft for this spec lives in:

- `docs/api-drafts/NGIN.Core-ApplicationModel.md`
- `docs/api-drafts/include/NGIN/Core/Application.hpp`
- `manifests/project.schema.json`

## Goals

- make startup feel the same across all application types
- reduce custom bootstrap code in individual applications
- support static modules and runtime plugins in one model
- make service registration, config, and lifecycle first-class platform concepts
- make startup deterministic and inspectable
- support both lightweight console tools and heavy editor/game hosts

## Non-Goals

- exact binary ABI tables for dynamic plugins
- editor UI layout details
- rendering, gameplay, or domain-specific APIs

## Core Concepts

### `ApplicationBuilder`

Mutable bootstrap object used before host creation.

Responsibilities:

- accept command-line inputs and environment inputs
- set the host profile
- register services
- add static modules
- reference packages
- configure plugin discovery
- configure logging, tasks, and config sources

### `ApplicationHost`

The built process host.

Responsibilities:

- own the resolved application composition
- start modules in deterministic order
- expose service access and runtime state
- run or tick depending on host style
- coordinate stop and shutdown

### `ServiceCollection`

Pre-build service registration surface.

Expected capabilities:

- explicit `Singleton`, `Scoped`, and `Transient` registrations
- explicit scopes
- optional reflection-backed activation

The first concrete draft makes this explicit through:

- `ServiceLifetime`
- `ServiceScopeKind`
- `AddSingleton(...)`
- `AddScoped(...)`
- `AddTransient(...)`

### `ModuleCollection`

Pre-build module registration surface.

Expected capabilities:

- add statically linked modules
- enable or disable modules by package/profile
- inspect resolved dependency order before host build

### `PluginCatalog`

Runtime-aware view of discovered plugin contributions.

Expected capabilities:

- enumerate candidate plugins
- validate compatibility
- activate compatible plugins
- report unsupported or rejected plugins with clear reasons

### `PackageCatalog`

A resolved set of packages available to the app.

Expected capabilities:

- discover installed packages
- expose package metadata
- surface modules, plugins, templates, and config defaults contributed by packages
- surface optional package bootstrap metadata

First concrete bootstrap implementation rule:

- package bootstrap discovery is static-link and registrar-based in v1

### `HostProfile`

A named mode that sets defaults without changing the architecture.

Initial profiles:

- `ConsoleApp`
- `GuiApp`
- `Game`
- `Editor`
- `Service`
- `TestHost`

Package bootstrap note:

- packages may expose a formal builder bootstrap hook through package metadata and `PackageBootstrapContext`
- linked package registrars are registered explicitly through the builder before `Build()`

## Recommended Builder Shape

Conceptually:

```cpp
int main(int argc, char** argv)
{
    auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);

    builder->UseProfile(NGIN::Core::HostProfile::Game);
    builder->Services().AddLogging().AddConfiguration();
    builder->Packages()
        .RegisterLinkedRegistrar(&NGIN_RegisterPackage_NGIN_ECS)
        .Add({"NGIN.ECS", ">=0.1.0 <1.0.0", false})
        .ApplyBootstrap("NGIN.ECS");
    builder->Modules().Enable("Core.Hosting").Enable("Domain.ECS");

    auto app = builder->Build();
    return app ? 0 : 1;
}
```

The first concrete draft now uses `NGIN.Core` naming only. The exact implementation may evolve, but the public shape and responsibilities should remain aligned with the draft artifacts.

## Host Lifecycle

The host lifecycle should be explicit and observable.

### Phase 1: Builder configuration

- collect inputs
- choose host profile
- register services
- add modules and package references
- configure plugin discovery and config sources

### Phase 2: Composition resolution

- resolve packages
- discover plugins
- construct the module graph
- validate host/profile compatibility
- validate platform and version compatibility

### Phase 3: Host build

- finalize service registrations
- prepare module activation plan
- prepare runtime subsystems
- produce a startable host object

### Phase 4: Start

- initialize core subsystems
- activate modules in resolved order
- emit lifecycle diagnostics and events

### Phase 5: Run

- run a console loop, service loop, editor loop, game loop, or one-shot program flow

### Phase 6: Stop and shutdown

- request stop
- unwind modules in reverse dependency order
- flush queued work where required
- dispose scopes and services deterministically

## Required Host Services

Every host should have platform-defined access to:

- logging
- configuration
- service resolution
- module metadata and state
- environment and profile information
- event publication/subscription
- task scheduling contracts

Shared project model note:

- `ngin.project.json` is the shared project manifest for the future CLI and VS Code extension
- `ngin.package.json` may declare a formal package bootstrap hook for builder-time setup
- direct target package references are the only packages auto-bootstrapped in v1

## Service Model

The service system should remain explicit and boring.

Requirements:

- typed registrations must work without reflection
- reflection-backed activation is optional, not mandatory
- service scopes must be deterministic
- host-owned services and module-owned services must have clear ownership rules

## Module Model

Modules should be first-class host citizens.

Requirements:

- identity and version
- dependency metadata
- host/profile compatibility
- startup and shutdown hooks
- service registration hook
- optional config contribution metadata

Modules should not rely on hand-built global registration tricks as the long-term model.

## Plugin Model

The host must support dynamic plugins, but plugin loading should sit inside a broader package model.

Requirements:

- compatibility validation before activation
- clear diagnostics when a plugin is skipped
- stable seam between host and plugin loader
- manifest-first discovery
- filesystem probing only as a secondary discovery mechanism

## Configuration Model

Configuration should be layered and inspectable.

Recommended precedence:

1. host defaults
2. package defaults
3. module defaults
4. environment/profile overrides
5. project/app config
6. command-line overrides
7. runtime mutable overrides where explicitly supported

## Observability

Host startup should produce enough information to debug composition problems.

Required diagnostics:

- resolved host profile
- package set
- plugin discovery results
- final module order
- compatibility failures
- missing service requirements
- shutdown unwind failures

## Current Implementation Mapping

Today the platform already contains a substantial subset of this host model in `NGIN.Core`, including:

- module resolution and ordering
- lifecycle orchestration
- services
- events
- tasks
- configuration

What is still missing at the platform level is the stronger product model around:

- `ApplicationBuilder` as the public center of gravity
- packages as the main distribution unit
- plugin loading as a complete workflow instead of a deferred seam
- host profiles as part of the product language

## Acceptance Criteria

This spec is satisfied when:

- a calculator, CLI tool, service, game, and editor can all start from the same host model
- applications no longer need bespoke bootstrap logic for common platform services
- host composition is inspectable before startup
- packages, modules, and plugins participate in one coherent startup pipeline
