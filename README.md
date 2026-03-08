# NGIN

**NGIN — Next Generation Infrastructure for eNGINes**

NGIN is a modular application foundation for C++.

It is built around a simple idea: a C++ application should start from a small, readable entrypoint, compose explicit services with clear lifetimes, and grow by adding well-defined packages instead of accumulating hidden globals and ad hoc startup code.

NGIN gives you:

- a clean host-based application model
- explicit service registration and lifetimes
- modular feature composition through packages
- XML manifests for projects and packages
- a native `ngin` CLI
- staged target output
- a first-class host/runtime implementation in `NGIN.Core`

NGIN is not trying to replace CMake. It is trying to give C++ applications a better composition model above the native build layer.

## The Pitch

NGIN is one way to build C++ applications with:

- small entrypoint
- explicit services
- explicit lifetimes
- modular features
- predictable startup
- less hidden state

If an application needs a window, logging, input, ECS, reflection, tools, or editor features, those capabilities should be added intentionally. They should come from packages and modules the application chooses, not from scattered initialization logic or implicit global state.

That is the value NGIN is aiming to provide.

## What NGIN Tries To Improve

In many C++ applications and engines, you may run into patterns like:

- startup code spread across many places
- static initialization and singleton-heavy architecture
- runtime wiring that is difficult to follow
- build dependencies and runtime features modeled separately
- reusable libraries without a clear application composition story

NGIN provides one way to structure that more cleanly.

The intended flow is:

`Project -> Target -> Packages -> Host`

You define the application, choose a target, add packages, and let the host compose the resulting application model.

## The Core Model

### Project

The overall application or product you are building.

Examples:

- `MyGame`
- `StudioTools`
- `TelemetryService`

A project contains one or more targets.

### Target

One concrete variant of a project.

Examples:

- `Game`
- `Editor`
- `DedicatedServer`
- `Tools`

A target says which packages are included and which platform/profile/environment that variant is built for.

The simplest way to think about it is:

- `Project` = the product
- `Target` = one build/run shape of that product

For example:

- Project: `MyGame`
- Targets:
  - `Game`
  - `Editor`
  - `DedicatedServer`

In that model, the editor and the runtime are usually different targets of the same project, not separate projects.

### Package

The main reusable unit in NGIN.

This is the most important concept.

A package can represent:

- a platform feature like `NGIN.Core`
- a domain package like `NGIN.ECS`
- a product feature like `NGIN.Editor`
- an external integration like `SDL2`
- your own app package like `MyGame.Runtime`

When you author a target, you mostly add packages.

### What Packages Can Provide

A package may provide:

- `Libraries`
  Build-time code exposed to the native backend.
  Example: `NGIN::Core`, `SDL2::SDL2`

- `Modules`
  Runtime behavior that participates in startup, dependency ordering, and host composition.
  Example: `Core.Hosting`, `Editor.Workspace`

- `Plugins`
  Optional runtime extensions owned by the package.

- `Content`
  Config files, assets, and staged files needed by the target.

### Host

The host is the runtime container that starts the resolved target.

Today, the active host implementation is `NGIN.Core`.

## Why This Matters

The point is not only to describe files better.

The point is to make application structure better:

- one understandable entrypoint
- explicit dependencies
- explicit service lifetimes
- explicit startup order
- feature composition through packages instead of hand wiring

NGIN is meant to make a C++ application feel intentionally composed instead of organically tangled.

## One Practical Example

If you are making a game, the flow should feel like this:

1. You create a project.
2. That project has a `Game` target.
3. The target depends on packages like `NGIN.Core`, `NGIN.ECS`, and `SDL2`.
4. Those packages bring the libraries, modules, services, and content the target needs.
5. NGIN validates the composition, stages the target, and hands it to the host.

That is the main idea.

## What You Can Do Today

At the current stage of the project, NGIN can already do useful platform work:

- author projects in `.nginproj`
- author packages in `.nginpkg`
- resolve package/module/plugin composition
- validate target composition
- inspect resolved graphs
- build staged target layouts
- emit `.ngintarget`
- use `NGIN.Core` as the current host/runtime foundation

The build backend is still CMake. The package wrapper and staging model are in place; broader package-driven native build orchestration is the next layer on top.

## Tiny Example

A target depending on `NGIN.Core`, `NGIN.ECS`, and `SDL2` looks like this:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="1" Name="MyGame" DefaultTarget="Game">
  <Targets>
    <Target Name="Game"
            Type="Runtime"
            Profile="Game"
            Platform="linux-x64"
            Environment="Dev"
            WorkingDirectory=".">
      <Packages>
        <PackageRef Name="NGIN.Core" VersionRange=">=0.1.0 <0.2.0" />
        <PackageRef Name="NGIN.ECS" VersionRange=">=0.1.0 <0.2.0" />
        <PackageRef Name="SDL2" VersionRange=">=2.30.0 <3.0.0" />
      </Packages>
    </Target>
  </Targets>
</Project>
```

That is the level NGIN wants you to work at first.

## This Repo’s Role

This repository is the umbrella workspace for the platform.

It owns:

- the active platform specs and architecture
- release and workspace metadata
- the native `ngin` CLI
- package wrappers in `Packages/`
- cross-repo validation, graphing, and staged build flow

It does not try to directly own all first-party source code.

- `NGIN.Core/` is first-class local platform code and is actively worked on here
- `NGIN.Base`, `NGIN.Log`, `NGIN.Reflection`, and `NGIN.ECS` remain independent repos
- those external first-party repos are integrated here through `Dependencies/` and exposed through package wrappers in `Packages/`

## Repository Shape

- `Packages/`
  The authoritative NGIN-facing integration layer.

- `Dependencies/`
  Source availability for repos integrated here but not owned here.

- `NGIN.Core/`
  The current host/runtime implementation and the main first-class component in this repo.

- `Tools/NGIN.CLI/`
  The source of the public `ngin` CLI.

- `manifests/`
  Workspace catalogs, release pins, and canonical example project manifests.

- `docs/`
  Architecture, examples, and active platform specs.

## Public Manifest Files

The active authored manifest family is XML:

- project: `.nginproj`
- package: `.nginpkg`
- staged target output: `.ngintarget`

`Project` and `Package` are the authored inputs. `.ngintarget` is generated by the build flow.

## Quick Start

1. Configure the workspace:

```bash
cmake --preset dev
```

2. Build the native CLI:

```bash
cmake --build build/dev --target ngin_cli
```

3. Check workspace health:

```bash
./build/dev/Tools/NGIN.CLI/ngin workspace doctor
./build/dev/Tools/NGIN.CLI/ngin workspace status
```

4. Validate and inspect the canonical workspace project:

```bash
./build/dev/Tools/NGIN.CLI/ngin project validate --project manifests/NGIN.Workspace.nginproj --target NGIN.CoreSample
./build/dev/Tools/NGIN.CLI/ngin project graph --project manifests/NGIN.Workspace.nginproj --target NGIN.EditorSample
```

5. Build a staged target layout:

```bash
./build/dev/Tools/NGIN.CLI/ngin project build --project manifests/NGIN.Workspace.nginproj --target NGIN.CoreSample --output build/manual/NGIN.CoreSample
```

6. Run the standard workspace flow through CMake:

```bash
cmake --build build/dev --target ngin.workflow
```

## Why The Structure Looks Like This

NGIN is intentionally split this way because these are different concerns:

- source ownership
- package exposure
- workspace orchestration
- runtime hosting

That is why:

- first-party libraries can stay independently useful outside NGIN
- NGIN can still expose them uniformly as packages
- `NGIN.Core` can stay first-class here
- the umbrella repo can stay focused on the platform experience instead of becoming a confused monorepo

## Read Next

- [NGIN Architecture](/home/berggrenmille/NGIN/docs/architecture/NGIN-Architecture.md)
- [NGIN Concepts](/home/berggrenmille/NGIN/docs/architecture/NGIN-Concepts.md)
- [Packages README](/home/berggrenmille/NGIN/Packages/README.md)
- [Dependencies README](/home/berggrenmille/NGIN/Dependencies/README.md)
- [Spec 001: Core Concepts and Vocabulary](/home/berggrenmille/NGIN/docs/specs/001-core-concepts.md)
- [Spec 003: Package Manifest and Runtime Contributions](/home/berggrenmille/NGIN/docs/specs/003-package-manifest-and-runtime-contributions.md)
- [Spec 006: CLI Contract](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md)
