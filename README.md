# NGIN

**NGIN — Next Generation Infrastructure for eNGINes**

NGIN is a modular application platform for modern C++ that helps you compose applications from explicit services, packages, and runtime hosts.

It provides a structured way to build applications with:

- explicit service lifetimes
- modular feature composition
- predictable startup behavior
- clean and minimal application entrypoints
- package-based application structure

Instead of wiring everything manually across build scripts, globals, and scattered initialization code, NGIN lets you compose applications from **projects, targets, and packages**.

NGIN is not primarily a package manager, and it is not trying to replace CMake. It is a composition model above the native build layer.

In NGIN, a package is the main unit of application composition. Packages describe what a target brings together at build time, runtime, and staging time: libraries, modules, plugins, tools, executables, and content.

NGIN encourages applications to start from a small, readable entrypoint and grow by composing explicit packages instead of accumulating hidden globals and ad hoc startup code.

---

## What NGIN Is

At a high level, NGIN applications are composed like this:

Project -> Target -> Packages -> Host

- **Project**: the product you are building
- **Target**: one runtime or build variant of that product
- **Packages**: the primary unit of application composition, used to bring in reusable features, integrations, modules, libraries, tools, executables, and staged content
- **Host**: the runtime that composes and starts everything

This flow is about application composition: packages describe what a target contains, and the host realizes that target as a runnable application shape.

Today, the active host implementation is `NGIN.Core`.

## Why NGIN Exists

In many C++ applications, you may run into patterns like:

- startup code spread across many places
- static initialization and singletons doing too much work
- runtime wiring that is difficult to follow
- unclear service lifetimes
- build dependencies and runtime composition modeled separately

NGIN provides one way to structure that more cleanly.

Applications are composed intentionally through **packages**, with:

- explicit dependencies
- predictable startup order
- clear service lifetimes
- modular feature composition

If an application needs a window, logging, input, ECS, reflection, tools, or editor features, those capabilities should be added intentionally instead of appearing through hidden global state or scattered initialization.

## Example

A simple game project might define a `Game` target composed from several packages:

- `NGIN.Core` — hosting and runtime foundation
- `NGIN.ECS` — entity component system
- `SDL2` — windowing and input integration

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

When this target is built, NGIN resolves the package graph, validates dependencies, and emits a staged `.ngintarget` layout that the host can execute.

## Core Concepts

### Project

The overall product you are building.

Examples:

- `MyGame`
- `StudioTools`
- `TelemetryService`

A project defines one or more **targets**.

### Target

One concrete variant of a project.

Examples:

- `Game`
- `Editor`
- `DedicatedServer`
- `Tools`

Targets define which **packages** are included and which platform, profile, and environment the variant is built for.

For example:

- Project: `MyGame`
- Targets:
  - `Game`
  - `Editor`
  - `DedicatedServer`

In that model, the editor and the shipped runtime are usually different targets of the same project, not separate projects.

### Package

The main reusable unit in NGIN.

Packages can represent:

- platform features like `NGIN.Core`
- domain features like `NGIN.ECS`
- product features like `NGIN.Editor`
- external integrations like `SDL2`
- your own app package like `MyGame.Runtime`

When defining a target, you primarily add packages.

### What Packages Can Provide

Packages may contribute:

- **Libraries**: native code exposed to the build backend
- **Modules**: runtime components participating in host startup and dependency ordering
- **Plugins**: optional runtime extensions
- **Content**: assets, configuration, and staged files
- **Executables**: application or tool binaries exposed by the package

### Host

The host is the runtime container that starts the resolved target.

Today, the active host/runtime foundation is `NGIN.Core`.

## CLI

The `ngin` CLI is the primary interface for working with projects, packages, and targets.

Examples:

Validate a target:

```bash
ngin project validate --project MyGame.nginproj --target Game
```

Inspect the resolved graph:

```bash
ngin project graph --project MyGame.nginproj --target Game
```

Build a staged target layout:

```bash
ngin project build --project MyGame.nginproj --target Game
```

## What You Can Do Today

At the current stage of the project, NGIN can:

- author projects in `.nginproj`
- author packages in `.nginpkg`
- resolve package, module, and plugin composition
- validate targets
- inspect dependency graphs
- build staged target layouts
- emit `.ngintarget` outputs
- run applications through the `NGIN.Core` host

The build backend is currently CMake. Package distribution and installation workflows are planned separately.

## Manifest Files

The active manifest family is XML:

- project: `.nginproj`
- package: `.nginpkg`
- planned package archive: `.nginpack`
- staged target output: `.ngintarget`

`Project` and `Package` are the authored inputs. `.ngintarget` is generated by the build flow. `.nginpack` is the planned installable package archive format, separate from the authored `.nginpkg` manifest.

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

6. Run the full workspace workflow:

```bash
cmake --build build/dev --target ngin.workflow
```

## Repository Layout

- **Packages/**  
  NGIN package wrappers used by the workspace.

- **Dependencies/**  
  External repositories integrated into the workspace.

- **NGIN.Core/**  
  The primary host/runtime implementation and the main first-class local component.

- **Tools/NGIN.CLI/**  
  The native `ngin` command line tool.

- **manifests/**  
  Workspace catalogs, release pins, and canonical example projects.

- **docs/**  
  Architecture notes, examples, and active platform specs.

## Why The Repo Is Split This Way

NGIN separates:

- source ownership
- package exposure
- workspace orchestration
- runtime hosting

That lets first-party libraries remain independently useful outside the umbrella workspace while still being exposed uniformly as packages.

## Documentation

- [NGIN Architecture](docs/architecture/NGIN-Architecture.md)
- [NGIN Concepts](docs/architecture/NGIN-Concepts.md)
- [Packages README](Packages/README.md)
- [Dependencies README](Dependencies/README.md)
- [Spec 001: Core Concepts and Vocabulary](docs/specs/001-core-concepts.md)
- [Spec 003: Package Manifest and Runtime Contributions](docs/specs/003-package-manifest-and-runtime-contributions.md)
- [Spec 006: CLI Contract](docs/specs/006-cli-contract.md)
- [Spec 009: Package Distribution and Installation](docs/specs/009-package-distribution-and-installation.md)
