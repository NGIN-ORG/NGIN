Here’s a **fully rewritten README** that keeps your intent, terminology, and structure—but fixes clarity, flow, and positioning. It leads with value, separates concepts cleanly, and makes the system understandable before introducing files and mechanics.

---

# NGIN

**NGIN — Next Generation Infrastructure for eNGINes**

NGIN is a composition model for modern C++ applications.

It provides a single, explicit way to define:

* what your application is made of
* how it is built
* how it starts and runs

Instead of scattering these concerns across build scripts, runtime code, and implicit initialization, NGIN brings them together into one authored model.

---

## Why NGIN Exists

In many C++ applications, common patterns emerge:

* startup logic spread across multiple systems
* static initialization and singletons doing too much work
* runtime wiring that is difficult to follow
* unclear or implicit service lifetimes
* build configuration and runtime composition modeled separately

These issues make applications harder to reason about, extend, and debug.

NGIN addresses this by making application structure **explicit and intentional**.

Applications are composed through **projects and packages**, with:

* explicit dependencies
* predictable startup order
* clear service lifetimes
* modular feature composition

If an application needs a window, logging, input, ECS, reflection, tools, or editor features, those capabilities are added deliberately—not introduced through hidden global state or scattered initialization.

---

## What NGIN Is

NGIN sits **above the native build layer** and defines how an application is composed.

* It uses CMake as its current build backend
* It does not replace compilers or linkers
* It is not primarily a package manager

Its core responsibility is **application composition**.

NGIN describes:

* the structure of your application
* the features it includes
* how those features are connected at runtime

---

## Core Model

An application in NGIN is defined through three main concepts:

### Project

A **project** is the thing you build.

Examples:

* `MyGame`
* `StudioTools`
* `TelemetryService`

A project defines one or more **variants**.

---

### Variant

A **variant** is a concrete configuration of a project.

Examples:

* `Game`
* `Editor`
* `DedicatedServer`
* `Tools`

Variants define:

* which packages are included
* target platform
* build profile
* runtime environment

In this model, different runtime modes (like editor vs game) are **variants of the same project**, not separate projects.

---

### Package

A **package** is the primary unit of composition.

Packages represent reusable capabilities:

* platform features (`NGIN.Core`)
* domain systems (`NGIN.ECS`)
* product features (`MyEngine.Editor`)
* external integrations (`SDL2`)
* application-specific logic (`MyGame.Runtime`)

When defining a project, you compose it by adding packages.

---

## What Packages Provide

Packages can contribute:

* **Libraries** — native code integrated into the build
* **Modules** — runtime components participating in startup and dependency ordering
* **Plugins** — optional runtime extensions
* **Content** — assets, configuration, and staged files
* **Executables** — tools or applications exposed by the package

---

## Example

A simple game project might define one application project with a `Game` variant composed from several packages:

* `NGIN.Core` — hosting and runtime foundation
* `NGIN.ECS` — entity component system
* `SDL2` — windowing and input integration

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

This describes the application as a composition of capabilities.

When built, NGIN:

* resolves the project and package graph
* generates the required CMake backend input
* produces a staged `.ngintarget` layout
* runs the application through the host

---

## Workspace

A **workspace** groups projects and package source roots.

It is defined by the top-level `.ngin` file and acts as the entry point for the system.

---

## Host

The **host** is the runtime container responsible for starting a resolved application.

* It composes modules
* enforces dependency ordering
* manages application lifetime

Today, the active host implementation is `NGIN.Core`.

---

## Manifest Files

NGIN uses XML-based manifests:

* `.ngin` — workspace definition
* `.nginproj` — project definition
* `.nginpkg` — package definition
* `.ngintarget` — generated staged output
* `.nginpack` — planned package archive format

`Workspace`, `Project`, and `Package` are authored inputs.
`.ngintarget` is produced by the build process.

---

## CLI

The `ngin` CLI is the primary interface for working with NGIN.

### Validate a variant

```bash
ngin project validate --project MyGame.nginproj --variant Game
```

### Inspect the resolved graph

```bash
ngin project graph --project MyGame.nginproj --variant Game
```

### Build a staged variant

```bash
ngin project build --project MyGame.nginproj --variant Game
```

---

## What You Can Do Today

NGIN currently supports:

* authoring workspaces, projects, and packages
* resolving composition graphs (projects, packages, modules, plugins)
* validating project variants
* inspecting dependency graphs
* generating build backend input (CMake)
* producing staged application layouts
* running applications through the `NGIN.Core` host

Package distribution and installation workflows are planned separately.

---

## Quick Start

1. Configure the workspace:

```bash
cmake --preset dev
```

1. Build the CLI:

```bash
cmake --build build/dev --target ngin_cli
```

1. Inspect workspace state:

```bash
./build/dev/Tools/NGIN.CLI/ngin workspace doctor
./build/dev/Tools/NGIN.CLI/ngin workspace list
./build/dev/Tools/NGIN.CLI/ngin workspace status
```

1. Validate and inspect an example project:

```bash
./build/dev/Tools/NGIN.CLI/ngin project validate --project Examples/App.Basic/App.Basic.nginproj --variant Runtime
./build/dev/Tools/NGIN.CLI/ngin project graph --project Examples/App.Basic/App.Basic.nginproj --variant Runtime
```

1. Build a staged variant:

```bash
./build/dev/Tools/NGIN.CLI/ngin project build --project Examples/App.Basic/App.Basic.nginproj --variant Runtime --output build/manual/App.Basic
```

1. Run the full workflow:

```bash
cmake --build build/dev --target ngin.workflow
```

---

## Repository Layout

* **Packages/**
  NGIN package wrappers used by the workspace

* **Dependencies/**
  External repositories integrated into the workspace

* **Tools/NGIN.CLI/**
  The `ngin` command line tool

* **NGIN.ngin**
  Root workspace file

* **Examples/**
  Sample projects and workspaces

* **docs/**
  Architecture notes and specifications

---

## Why the Repository Is Structured This Way

NGIN separates:

* source ownership
* package exposure
* workspace orchestration
* runtime hosting

This allows libraries to remain independently usable while still being exposed consistently as packages.

`Packages/NGIN.Core/` contains the current host/runtime implementation.
It exists alongside other packages to keep the repository structure uniform.

---

## Documentation

* [NGIN Architecture](docs/architecture/NGIN-Architecture.md)
* [NGIN Concepts](docs/architecture/NGIN-Concepts.md)
* [Packages README](Packages/README.md)
* [Dependencies README](Dependencies/README.md)
* [Spec 001: Core Concepts and Vocabulary](docs/specs/001-core-concepts.md)
* [Spec 003: Package Manifest and Runtime Contributions](docs/specs/003-package-manifest-and-runtime-contributions.md)
* [Spec 006: CLI Contract](docs/specs/006-cli-contract.md)
* [Spec 009: Package Distribution and Installation](docs/specs/009-package-distribution-and-installation.md)

---

If you want, next step I can:

* make a **short “landing README” + deeper docs split** (very GitHub-friendly), or
* sharpen this further for **engine dev audience vs general C++ audience**.
