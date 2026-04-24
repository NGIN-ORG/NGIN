# NGIN

NGIN is a modular application platform for modern C++. It gives C++ projects a clearer authored shape: optional workspace files to organize a repo, project files that own the buildable app or library boundary, package files for reusable capabilities, and generated tooling metadata for staged run/debug flows.

NGIN is not trying to replace CMake or pretend C++ should work like a managed runtime. The goal is narrower: make application composition, startup shape, and staged output more explicit than the usual mix of handwritten build logic, scattered initialization, and implicit runtime wiring.

The center of gravity is composition: a selected project configuration, together with resolved project and package contributions, produces a concrete runtime shape that can be validated, graphed, staged, and launched.

NGIN has two separable layers:

- NGIN tooling: manifests, resolution, build generation, staging, validation, graphing, local run/debug, and package workflows
- `NGIN.Core`: an optional hosted application runtime for services, configuration, modules, plugins, lifecycle, reflection, and diagnostics

Plain C++ applications can be built and staged by NGIN without linking `NGIN.Core`. Applications use `NGIN.Core` only when they want the hosted startup model.

## Why NGIN Exists

In a typical native codebase, several concerns tend to blur together:

- build dependencies
- runtime composition
- startup order
- environment and config selection
- the boundary between “this app” and “this reusable feature”

That usually works for a while, then starts to cost time. It becomes harder to answer simple questions like:

- what is the buildable unit here
- which features belong to the app versus a reusable dependency
- how does this program decide which runtime shape to start
- what files and binaries are supposed to end up in the staged output

NGIN provides one way to make those answers visible in authored metadata. It does that with a deliberately small model:

- `.ngin` for an optional workspace
- `.nginproj` for the buildable project
- `.nginpkg` for reusable packages
- `.nginlaunch` for generated staged output

The active host/runtime implementation in this repo is `NGIN.Core`.

`ngin build` does not just emit binaries. It produces a fully materialized runnable directory and a generated `.nginlaunch` file that captures the launchable result of the resolved composition.

## Core Authored Model

### Workspace

A workspace is the optional repo-level container. It answers questions like:

- which projects belong to this working tree
- where package manifests are discovered
- which local dependency source trees override package roots through `PackageProviders`

Single-project authoring does not require a workspace file, but the umbrella repo uses one to tie examples, package wrappers, and local dependency trees together.

### Project

A project is the primary buildable authored unit. In practice, that means an application, tool, or library with its own source roots, output, references, and named configurations.

This is the main V2 shift: if you have separate executables, they are usually separate projects. A game client and a headless server are different projects. A game engine library is either another project or a package, depending on whether you want it to remain a local build unit or a reusable published dependency.

### Configuration

A configuration is one named setup of the same project. It is intentionally narrow. It selects build and launch details such as:

- `BuildConfiguration`
- `OperatingSystem`
- `Architecture`
- `Environment`
- `Launch`
- reference, config, module, or plugin overlays

Configurations are not meant to model unrelated apps. They are for changes in setup, not for inventing extra buildable identities.

`Environment` selects a named runtime layer that can contribute config sources, variables, features, contents, and runtime overlays.

### Package

A package is the reusable unit. Packages can expose libraries, executables, modules, plugins, content, and bootstrap behavior. In V2, packages stay focused on reusable identity and declared contributions through explicit manifest sections. Workspace-local source overrides now come from workspace `PackageProviders`, not from package-level source binding metadata.

Packages are versioned dependency units. The active expectation is one resolved version per package identity within a single composition.

As a practical rule: keep something as a project while it is a local buildable unit owned primarily by one repo. Make it a package when it needs reusable identity, dependency-style consumption, and independent versioning across projects.

### Launch Manifest

`ngin build` emits `<Project>.<Configuration>.nginlaunch` in the staged output directory. That file captures the resolved launchable result for local tooling: the selected executable, working directory, staged files, and resolved composition metadata.

You should think of `.nginlaunch` as generated local tooling metadata, not as another authored input file and not as a required production runtime dependency. It exists for `ngin run`, editor debug integration, inspection, and smoke tests. A shipped application is still a normal native layout: executable, required libraries, assets, config, plugins, and content.

## Example Project

The shape below is the normal V2 path: one project owns the executable boundary, references a reusable package, and optionally references another local project. The selected configuration decides how that same project is launched.

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="MyGame"
         Type="Application"
         DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>

  <Output Kind="Executable"
          Name="MyGame"
          Target="MyGame" />

  <References>
    <Project Path="../Game.Engine/Game.Engine.nginproj" />
    <Package Name="NGIN.Core" Version="0.1.0" />
  </References>

  <Environments>
    <Environment Name="development" />
  </Environments>

  <Configurations>
    <Configuration Name="Runtime"
                   BuildConfiguration="Debug"
                   OperatingSystem="linux"
                   Architecture="x64"
                   Environment="development">
      <Launch Executable="MyGame" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
```

That should be read as:

- `MyGame` is the buildable app
- `Game.Engine` is another local project
- `NGIN.Core` is a reusable package dependency
- `Runtime` is one launch/build setup of `MyGame`, not a separate executable identity

## CLI Overview

The native CLI is the main interface for working with the authored model. The active surface is:

- `ngin build [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin clean [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin rebuild [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin run [--project <file>] [--configuration <name>] [--output <dir>] [-- <args...>]`
- `ngin validate [--project <file>] [--configuration <name>]`
- `ngin graph [--project <file>] [--configuration <name>]`
- `ngin package list`
- `ngin package show <Package>`
- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`

The normal flow is:

1. author or open a project
2. select a configuration
3. validate or inspect the graph
4. build to a staged output directory
5. clean or rebuild when you need to reset generated artifacts for that scope
6. run from the generated `.nginlaunch`

`ngin graph` is the current structural inspection surface for composition. Longer-term CLI inspection should answer not just "what is in the graph" but also "why is this here".

## What You Can Do Today

At the current state of the repo, NGIN can:

- author workspaces, projects, and packages in XML
- resolve project and package references
- apply configuration-level overlays
- validate configuration selection and launchability
- generate backend input for generated project builds
- stage outputs, content, and config into a launchable directory
- emit `.nginlaunch` as the local tooling handoff artifact
- run plain native applications or applications that use the optional `NGIN.Core` hosted runtime

NGIN prefers explicit validation failure over implicit or ambiguous runtime resolution. If authored inputs do not resolve to a clear launchable result, validation should fail instead of silently inventing behavior.

The active build backend is CMake. The normal application path does not require a handwritten project `CMakeLists.txt`; the project manifest owns the build-facing metadata NGIN needs.

## Manifest File Family

The active file family is:

- `.ngin` for workspaces
- `.nginproj` for projects
- `.nginpkg` for packages
- `.nginlaunch` for generated staged launches
- `.nginpack` as the planned installable package archive format

The first three are authored inputs. `.nginlaunch` is produced by the build flow.

## Quick Start

Configure the workspace:

```bash
cmake --preset dev
```

Build the native CLI:

```bash
cmake --build build/dev --target ngin_cli
```

Check workspace health:

```bash
./build/dev/Tools/NGIN.CLI/ngin workspace doctor
./build/dev/Tools/NGIN.CLI/ngin workspace list
./build/dev/Tools/NGIN.CLI/ngin workspace status
```

Validate and inspect the canonical first example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime

./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime
```

Build and run a staged example:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic
```

If you want to see how separate executables are modeled in V2, inspect `Examples/Game.Engine`, `Examples/Game.Client`, and `Examples/Game.Server` next.

If you want to see the difference between NGIN tooling and the optional hosted runtime, inspect `Examples/App.NativeMinimal` and `Examples/App.HostedCore`.

## Repository Layout

- `Tools/NGIN.CLI/` native `ngin` command line tool
- `Tools/NGIN.VSCode/` in-repo VS Code extension
- `Packages/NGIN.Core/` local host/runtime package
- `Packages/*.nginpkg` package wrappers and package metadata
- `Examples/` canonical authored examples
- `Dependencies/` local first-party and third-party source trees
- `docs/specs/` active contracts

The root workspace file is [NGIN.ngin](/home/berggrenmille/NGIN/NGIN.ngin). It uses `PackageSources` and `PackageProviders` to expose both wrapper packages under `Packages/` and local dependency source trees such as `Dependencies/NGIN/NGIN.Base`.

## Specs

The active contracts live in:

- [001-core-concepts.md](/home/berggrenmille/NGIN/docs/specs/001-core-concepts.md)
- [002-project-and-target-manifest.md](/home/berggrenmille/NGIN/docs/specs/002-project-and-target-manifest.md)
- [003-package-manifest-and-runtime-contributions.md](/home/berggrenmille/NGIN/docs/specs/003-package-manifest-and-runtime-contributions.md)
- [004-composition-and-validation.md](/home/berggrenmille/NGIN/docs/specs/004-composition-and-validation.md)
- [005-staged-target-manifest.md](/home/berggrenmille/NGIN/docs/specs/005-staged-target-manifest.md)
- [006-cli-contract.md](/home/berggrenmille/NGIN/docs/specs/006-cli-contract.md)
- [007-host-integration-contract.md](/home/berggrenmille/NGIN/docs/specs/007-host-integration-contract.md)
- [010-workspace-and-project-model.md](/home/berggrenmille/NGIN/docs/specs/010-workspace-and-project-model.md)
- [011-workspace-manifest.md](/home/berggrenmille/NGIN/docs/specs/011-workspace-manifest.md)
- [012-tooling-and-runtime-boundary.md](/home/berggrenmille/NGIN/docs/specs/012-tooling-and-runtime-boundary.md)
