# NGIN Platform Direction

Status: Draft opinionated direction  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-05

## Executive Thesis

NGIN should be treated as an **application platform**, not as a game-engine-only runtime.

That means:

- a game engine editor is an NGIN application
- a game is an NGIN application
- a calculator is an NGIN application
- a CLI tool is an NGIN application
- a service/daemon is an NGIN application

The common value is not rendering or gameplay. The common value is:

- app bootstrapping
- service registration and dependency injection
- module/plugin loading
- configuration
- events/tasks/lifecycle
- build/package/tooling integration

In short: `NGIN` should become for C++ projects what the .NET generic host plus SDK ecosystem is for .NET projects, with a stronger module/plugin bias.

## Primary Recommendation

Build NGIN around a **hosted application model**.

Every app should start from the same conceptual pattern:

1. create an application builder
2. register services
3. add modules/plugins
4. configure the host
5. build the application
6. run it

Conceptually:

```cpp
int main(int argc, char** argv)
{
    auto builder = NGIN::CreateApplicationBuilder(argc, argv);

    builder.Services().AddLogging();
    builder.Services().AddConfiguration();
    builder.Modules().AddStaticModule<MyModule>();
    builder.Plugins().LoadFromManifest("plugins/");

    auto app = builder.Build();
    return app.Run();
}
```

This should be the center of the platform identity.

## What NGIN Should Be

NGIN should be split mentally into four layers.

### 1. Foundation Libraries

These remain broadly useful outside the full platform:

- `NGIN.Base`
- `NGIN.Log`
- `NGIN.Reflection`
- similar reusable libraries in the future

These should stay separate because they are general-purpose building blocks.

### 2. Core Hosting Layer

This is the real heart of the platform:

- application builder
- host lifecycle
- service container
- module loader
- plugin loader
- configuration system
- event bus
- task scheduler contract
- environment/profile handling

This layer is what gives NGIN its identity.

### 3. Product/Domain Packages

These are optional packages built on top of the core hosting layer:

- engine/runtime features
- editor framework
- ECS
- assets/content pipeline
- rendering
- networking stacks
- scripting bridges
- diagnostics/devtools

These should be composable, not mandatory.

### 4. Applications

These are final deliverables:

- editor executable
- game executable
- CLI tool
- calculator
- backend service

The application should mostly be composition and configuration, not a hand-built bootstrap every time.

## Naming Recommendation

My opinion: `NGIN.Core` is not the best long-term name for the current kernel/host library.

Why:

- "runtime" sounds like the final running app layer, especially in game-engine language
- it does not clearly communicate app composition, hosting, services, and bootstrapping
- if you later build an actual game runtime, the naming becomes muddy

## Recommended Naming Options

### Preferred simple option

Rename `NGIN.Core` to `NGIN.Core`.

Use `NGIN.Core` for:

- application host
- builder
- modules/plugins
- services
- lifecycle
- config/events/tasks

This is the clearest match to your stated vision.

### Preferred precise option

Split today’s conceptual `Runtime` into two names:

- `NGIN.Core`: shared contracts and orchestration primitives
- `NGIN.Hosting`: default host/builder/bootstrap implementation

This is architecturally cleaner, but only worth it if you want stricter layering.

### What I would avoid

- keeping `NGIN.Core` as the central platform identity
- using `NGIN` alone as a library name
- using `Runtime` to mean both host kernel and final app runtime

If you want the fastest path with the least conceptual confusion, rename it to `NGIN.Core`.

## Game Engine and Editor Fit

Yes, they fit. They fit very naturally, but they should not define the platform.

The right framing is:

- `NGIN` is the platform
- the game engine is one major product built on the platform
- the editor is another application built on the platform
- game runtime/player builds are applications built on the platform

This is important because it prevents engine-specific assumptions from leaking into everything else.

The editor should not be a special case. It should be an NGIN app with:

- editor host profile
- editor modules
- UI panels/tools as plugins
- asset/database/project services

The game should also not be a special case. It should be an NGIN app with:

- runtime/game host profile
- game modules
- engine modules
- content/runtime packaging rules

That gives you one platform and many products instead of one engine trying to impersonate a platform.

## Recommended Product Model

I would define NGIN as a product family.

### Platform

- `NGIN` = umbrella platform, tooling, manifests, SDK experience

### Core packages

- `NGIN.Base`
- `NGIN.Log`
- `NGIN.Reflection`
- `NGIN.Core` or `NGIN.Hosting`

### Optional first-party packages

- `NGIN.ECS`
- `NGIN.Editor`
- `NGIN.Assets`
- `NGIN.Build`
- `NGIN.Diagnostics`
- `NGIN.Rendering`
- `NGIN.Scripting`

### Final applications

- `NGIN.EditorApp`
- `MyGame`
- `MyCalculator`
- `MyCliTool`

This avoids collapsing everything into one giant library while still presenting a coherent platform.

## Plugin and Module Direction

Your plugin/module instinct is correct. It should be a core platform assumption, not an afterthought.

I recommend three levels of composition.

### 1. Static modules

Compiled into the application.

Use these for:

- core product functionality
- stable internal modules
- platform bring-up

### 2. Dynamic plugins

Loaded at runtime from packages/manifests.

Use these for:

- editor tools
- optional integrations
- diagnostics
- extension packs
- third-party ecosystem features

### 3. Packages

A package should be a distributable unit that may contain:

- one or more modules
- zero or more dynamic plugin binaries
- manifests
- assets/templates
- commands/tooling metadata

This matters because plugin loading alone is too narrow. You want an ecosystem, not just shared libraries on disk.

## Module Rules I Would Standardize Early

- Every module has an identity, version, and dependency list.
- Every module declares the host types it supports.
- Every module can register services during bootstrap.
- Modules should be enabled/disabled by manifest/profile, not by ad hoc code paths.
- Plugins should be discoverable by manifest first, filesystem second.
- Host startup should produce a deterministic module graph and startup order report.

That last point is important. If NGIN is foundational for all your apps, startup must feel inspectable and trustworthy.

## Host Types I Would Support Explicitly

Define host kinds as a first-class platform concept.

- `ConsoleApp`
- `GuiApp`
- `Game`
- `Editor`
- `Service`
- `TestHost`

These should affect defaults, not fork the architecture.

Examples:

- `Editor` enables reflection, diagnostics, layout persistence, and tool plugins by default
- `Game` uses runtime-lean defaults
- `Service` disables UI/editor-specific systems
- `ConsoleApp` enables command routing and text output integrations

## Builder-First API Direction

The most important API you can design next is not the low-level plugin ABI.
It is the application builder and host lifecycle API.

I would define these concepts before going deeper on implementation:

- `ApplicationBuilder`
- `Application`
- `HostEnvironment`
- `ServiceCollection`
- `ModuleCollection`
- `PluginCatalog`
- `PackageCatalog`
- `HostProfile`

If those are clear, the rest of the platform will align more easily.

## Tooling Direction

This is where NGIN can become genuinely pleasant to use.

I recommend treating tooling as a first-class product, not a support script.

### Needed tooling surface

- `ngin new`
- `ngin build`
- `ngin run`
- `ngin test`
- `ngin package`
- `ngin publish`
- `ngin doctor`
- `ngin graph`
- `ngin plugins`

### What the CLI should understand

- workspace manifests
- project templates
- package references
- module graphs
- target profiles
- build presets
- editor/game/service application kinds

### VS Code extension direction

The VS Code extension should not just shell out to build commands.
It should understand NGIN concepts:

- create project from template
- add package/module/plugin reference
- switch host profile
- run build/package graph diagnostics
- launch editor/game/tool targets
- inspect resolved module graph
- scaffold new module/plugin/package

That would make NGIN feel like a platform rather than a loose repo collection.

## Packaging Direction

If your end goal is "NGIN in all my apps and services", packaging must be designed early.

I recommend a package model with:

- package manifest
- semantic versioning
- platform/ABI constraints
- dependency graph
- local cache
- publishable artifacts

A package should be able to deliver:

- headers/libs
- runtime binaries
- plugin bundles
- templates
- tooling metadata

This package system does not need to be internet-scale at first. It only needs to be coherent and predictable.

## Repository and Workspace Direction

I would keep the current split between reusable component repos if that separation is useful to you.

But the user experience should feel like one platform:

- one workspace manifest
- one CLI
- one package story
- one host model
- one extension story

In other words, polyrepo internally is fine. Fragmented product identity is not.

## Opinionated Architectural Boundaries

These are the boundaries I would enforce.

### Keep in foundation libs

- containers
- memory
- async primitives
- IO/path/filesystem
- serialization primitives
- logging
- reflection

### Keep in core hosting

- app builder
- module/plugin/package loading
- service registration/resolution
- lifecycle orchestration
- configuration graph
- event/task contracts
- application profiles

### Keep out of core hosting

- renderer-specific systems
- editor widgets and layouts
- game-framework assumptions
- asset import pipelines
- domain-specific gameplay logic

If `NGIN.Core` stays clean, everything above it will age better.

## What I Would Build Next

If we ignore breaking changes and optimize for the right future shape, I would do this next.

### Phase 1: Lock the product language

- decide whether the central library is `NGIN.Core` or `NGIN.Hosting`
- define what `NGIN` means at the umbrella level
- define host kinds and package/plugin/module terminology
- stop mixing "runtime", "core", "host", and "platform" loosely

### Phase 2: Define the builder/host contract

- write a spec for the application builder
- write a spec for service/module/plugin registration flow
- define startup/shutdown lifecycle clearly
- define configuration and environment/profile behavior

### Phase 3: Define the package model

- package manifest format
- package references and dependency resolution
- local package cache layout
- plugin bundle/package relationship

### Phase 4: Build the CLI around the platform model

- scaffold projects
- resolve graph
- build/run/package targets
- inspect startup composition

### Phase 5: Build editor and engine as first-party proof

- build the editor as an NGIN application
- build the engine runtime/player as an NGIN application
- use both to validate that the platform is truly general-purpose

### Phase 6: Build the VS Code integration

- project creation
- package/module/plugin authoring workflows
- target launching
- graph diagnostics

## Concrete Recommendation For This Repo

I would evolve this umbrella repo into the place that defines:

- platform identity
- package/module/plugin schemas
- workspace tooling
- templates
- compatibility manifests
- platform specs

I would avoid making this repo pretend to be the single SDK library.
That would blur responsibilities.

## Final Opinion

The strongest version of your vision is:

NGIN is a **general application platform for C++**, with a **builder-driven host model**, a **module/plugin/package ecosystem**, and enough tooling that every project feels like it belongs to the same operating environment.

The game engine and editor should absolutely be built with it, but they should be proofs of the platform, not the definition of the platform.

If I had to choose one sentence to steer all future decisions, it would be this:

**Design NGIN so that starting a calculator, a CLI tool, an editor, or a game feels like the same platform experience with different modules and host profiles.**
