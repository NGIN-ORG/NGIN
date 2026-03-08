# NGIN

**NGIN — Next Generation Infrastructure for eNGINes**

NGIN is a C++ platform for building engines, tools, runtimes, editors, and applications around one shared model.

It gives you:

- a package-centric application model
- XML manifests for projects and packages
- a native `ngin` CLI
- staged target output
- a first-class host/runtime implementation in `NGIN.Core`

NGIN is not trying to replace CMake. It is trying to replace ad hoc integration, scattered runtime metadata, and unclear boundaries between build-time and runtime composition.

## What Problem NGIN Solves

Large C++ codebases usually end up with some combination of:

- raw CMake targets as the only integration model
- custom bootstrap code per application
- runtime modules/plugins described separately from build dependencies
- poor separation between reusable libraries and reusable application features

NGIN tries to give that a cleaner shape.

The intended flow is:

`Project -> Target -> Packages -> Host`

You describe the application in terms of projects, targets, and packages. NGIN then resolves what code, runtime behavior, and staged content that target needs.

## The Core Model

### Project

Your application.

Examples:

- a game
- an editor
- a server
- a developer tool

### Target

One concrete variant of that project.

Examples:

- `Game`
- `Editor`
- `Server`

A target says which packages are included and which platform/profile/environment they are built for.

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
  Build-time code artifacts exposed to the native build backend.
  Example: `NGIN::Core`, `SDL2::SDL2`

- `Modules`
  Runtime behavior that participates in startup, dependency ordering, and host composition.
  Example: `Core.Hosting`, `Editor.Workspace`

- `Plugins`
  Optional runtime extensions owned by the package.

- `Content`
  Config files, assets, and staged files needed by the target.

### Host

The runtime container that starts the resolved target.

Today, the active host implementation is `NGIN.Core`.

## One Practical Example

If you are making a game, the flow should feel like this:

1. You create a project.
2. That project has a `Game` target.
3. The target depends on packages like `NGIN.Core`, `NGIN.ECS`, and `SDL2`.
4. Those packages bring the libraries, modules, and content the target needs.
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
