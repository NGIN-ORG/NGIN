## NGIN

**NGIN — Next Generation Infrastructure for eNGINes**

NGIN is a modular application platform for modern C++. It gives C++ applications a clearer authored shape:

- a workspace file to organize projects
- project files that own source roots and outputs
- package files for reusable capabilities
- a host/runtime that composes the resolved application

NGIN is not primarily a package manager, and it is not trying to replace CMake. It is a composition and build model above the native backend layer, with CMake as the active backend.

The active authored split is:

- `.ngin` for workspaces
- `.nginproj` for projects
- `.nginpkg` for packages
- `.ngintarget` for generated staged outputs

Today, the active host implementation is `NGIN.Core`.

## Why NGIN Exists

In many C++ applications, you may run into patterns like:

- startup code spread across many places
- static initialization and singletons doing too much work
- runtime wiring that is difficult to follow
- unclear service lifetimes
- build dependencies and runtime composition modeled separately

NGIN provides one way to structure that more cleanly.

Applications are composed intentionally through **projects and packages**, with:

- explicit dependencies
- predictable startup order
- clear service lifetimes
- modular feature composition

If an application needs a window, logging, input, ECS, reflection, tools, or editor features, those capabilities should be added intentionally instead of appearing through hidden global state or scattered initialization.

## Example

A simple game project might define one application project with a `Game` variant composed from several packages:

- `NGIN.Core` — hosting and runtime foundation
- `NGIN.ECS` — entity component system
- `SDL2` — windowing and input integration

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="1"
         Name="MyGame"
         Type="Application"
         DefaultVariant="Game">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <PrimaryOutput Kind="Executable"
                 Name="MyGame"
                 Target="MyGame" />
  <Build Backend="CMake"
         Mode="Generated"
         Language="CXX"
         LanguageStandard="23" />
  <PackageRefs>
    <PackageRef Name="NGIN.Core" VersionRange=">=0.1.0 <0.2.0" />
    <PackageRef Name="NGIN.ECS" VersionRange=">=0.1.0 <0.2.0" />
    <PackageRef Name="SDL2" VersionRange=">=2.30.0 <3.0.0" />
  </PackageRefs>
  <Variants>
    <Variant Name="Game"
             Profile="Game"
             Platform="linux-x64"
             Environment="Dev"
             WorkingDirectory=".">
      <Launch Executable="MyGame" />
    </Variant>
  </Variants>
</Project>
```

When this variant is built, NGIN resolves the project and package graph, generates the required CMake backend input for generated-mode projects, and emits a staged `.ngintarget` layout that the host can execute.

## Core Concepts

### Workspace

The top-level `.ngin` file that groups projects and package source roots.

### Project

The overall buildable application, library, or tool you are building.

Examples:

- `MyGame`
- `StudioTools`
- `TelemetryService`

A project defines one or more **variants**.

### Variant

One concrete variant of a project.

Examples:

- `Game`
- `Editor`
- `DedicatedServer`
- `Tools`

Variants define which additional **packages** are included and which platform, profile, and environment the variant is built for.

For example:

- Project: `MyGame`
- Variants:
  - `Game`
  - `Editor`
  - `DedicatedServer`

In that model, the editor and the shipped runtime are usually different variants of the same project, not separate projects.

### Package

The main reusable unit in NGIN.

Packages can represent:

- platform features like `NGIN.Core`
- domain features like `NGIN.ECS`
- product features like `MyEngine.Editor`
- external integrations like `SDL2`
- your own app package like `MyGame.Runtime`

When defining a project, you primarily add packages and then choose a variant.

### What Packages Can Provide

Packages may contribute:

- **Libraries**: native code exposed to the build backend
- **Modules**: runtime components participating in host startup and dependency ordering
- **Plugins**: optional runtime extensions
- **Content**: assets, configuration, and staged files
- **Executables**: application or tool binaries exposed by the package

### Host

The host is the runtime container that starts the resolved project variant.

Today, the active host/runtime foundation is `NGIN.Core`.

## CLI

The `ngin` CLI is the primary interface for working with workspaces, projects, packages, and variants.

Examples:

Validate a variant:

```bash
ngin project validate --project MyGame.nginproj --variant Game
```

Inspect the resolved graph:

```bash
ngin project graph --project MyGame.nginproj --variant Game
```

Build a staged variant layout:

```bash
ngin project build --project MyGame.nginproj --variant Game
```

## What You Can Do Today

At the current stage of the project, NGIN can:

- author workspaces in `.ngin`
- author projects in `.nginproj`
- author packages in `.nginpkg`
- resolve project, package, module, and plugin composition
- validate variants
- inspect dependency graphs
- build staged project layouts
- emit `.ngintarget` outputs
- run applications through the `NGIN.Core` host

The build backend is currently CMake. Package distribution and installation workflows are planned separately. Normal application projects do not need a handwritten project `CMakeLists.txt`; `ngin project build` generates that backend layer from `.nginproj` metadata.

## Manifest Files

The active manifest family is XML:

- workspace: `.ngin`
- project: `.nginproj`
- package: `.nginpkg`
- planned package archive: `.nginpack`
- staged target output: `.ngintarget`

`Workspace`, `Project`, and `Package` are the authored inputs. `.ngintarget` is generated by the build flow. `.nginpack` is the planned installable package archive format, separate from the authored `.nginpkg` manifest.

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
./build/dev/Tools/NGIN.CLI/ngin workspace list
./build/dev/Tools/NGIN.CLI/ngin workspace status
```

4. Validate and inspect the canonical example project:

```bash
./build/dev/Tools/NGIN.CLI/ngin project validate --project Examples/App.Basic/App.Basic.nginproj --variant Runtime
./build/dev/Tools/NGIN.CLI/ngin project graph --project Examples/App.Basic/App.Basic.nginproj --variant Runtime
```

5. Build a staged project variant:

```bash
./build/dev/Tools/NGIN.CLI/ngin project build --project Examples/App.Basic/App.Basic.nginproj --variant Runtime --output build/manual/App.Basic
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

- **Tools/NGIN.CLI/**  
  The native `ngin` command line tool.

- **NGIN.ngin**  
  The root workspace file for the repo.

- **Examples/**  
  Canonical sample workspaces and projects used by docs, smoke tests, and CLI examples.

- **docs/**  
  Architecture notes, examples, and active platform specs.

## Why The Repo Is Split This Way

NGIN separates:

- source ownership
- package exposure
- workspace orchestration
- runtime hosting

That lets first-party libraries remain independently useful outside the umbrella workspace while still being exposed uniformly as packages.

`Packages/NGIN.Core/` is the current locally owned host/runtime package. It lives beside the other package directories so the repo surface stays package-centric, even though `NGIN.Core` remains a special platform component in role.

## Documentation

- [NGIN Architecture](docs/architecture/NGIN-Architecture.md)
- [NGIN Concepts](docs/architecture/NGIN-Concepts.md)
- [Packages README](Packages/README.md)
- [Dependencies README](Dependencies/README.md)
- [Spec 001: Core Concepts and Vocabulary](docs/specs/001-core-concepts.md)
- [Spec 003: Package Manifest and Runtime Contributions](docs/specs/003-package-manifest-and-runtime-contributions.md)
- [Spec 006: CLI Contract](docs/specs/006-cli-contract.md)
- [Spec 009: Package Distribution and Installation](docs/specs/009-package-distribution-and-installation.md)
