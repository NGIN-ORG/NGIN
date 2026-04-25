# NGIN

NGIN is a C++ application composition and staging toolchain. It gives a native
project an authored shape: a project manifest says what the buildable unit is,
which packages or local projects it references, how it should be configured, and
what a runnable staged output should contain.

NGIN is not a replacement for CMake. The current build backend is CMake, and the
normal application path lets a `.nginproj` generate the backend build input
instead of requiring every application project to carry a handwritten
`CMakeLists.txt`.

There are two separate layers:

- NGIN tooling: manifests, resolution, build generation, validation, graphing,
  staging, local run/debug, workspace inspection, and package workflows.
- `NGIN.Core`: an optional hosted runtime for services, configuration, modules,
  plugins, lifecycle, reflection, and diagnostics.

Plain C++ applications can use the tooling layer without linking `NGIN.Core`.
Applications link `NGIN.Core` only when they want the hosted startup model.

## Quick Start

This repository builds the native `ngin` CLI from source, then uses it to
validate, build, stage, and run an example application.

Prerequisites for building from this checkout:

- CMake 3.20 or newer
- Ninja
- a C++23-capable compiler
- a normal C++ development environment for your platform

Configure the workspace and build the CLI:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Check that the CLI sees the workspace:

```bash
./build/dev/Tools/NGIN.CLI/ngin workspace doctor
./build/dev/Tools/NGIN.CLI/ngin workspace list
./build/dev/Tools/NGIN.CLI/ngin workspace status
```

Validate and inspect the smallest application example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --configuration Runtime

./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --configuration Runtime
```

Build and run it from a staged output directory:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --configuration Runtime \
  --output build/manual/App.NativeMinimal

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --configuration Runtime \
  --output build/manual/App.NativeMinimal
```

That flow proves the core tooling path: a `.nginproj` can describe a normal C++
executable, generate backend build input, produce a runnable staged directory,
and emit local launch metadata.

## What Just Happened

`ngin validate` checks that the selected project configuration resolves to a
clear composition. A composition is the concrete application shape produced from
one project configuration plus its referenced projects, packages, config,
content, runtime metadata, and launch settings.

`ngin graph` prints the resolved structure so you can inspect what contributed to
that composition.

`ngin build` does more than compile a binary. It materializes a staged directory
with the built executable, resolved package artifacts, config/content, and a
generated `.nginlaunch` file.

`ngin run` consumes that generated `.nginlaunch` file. The launch file is local
tooling metadata for run/debug/smoke-test flows; it is not an authored project
input and it is not required as a production runtime dependency.

## Learning Path

The examples are arranged so each one adds one idea:

1. [`Examples/App.NativeMinimal`](Examples/App.NativeMinimal/README.md)
   Smallest plain native executable. No `NGIN.Core`.
2. [`Examples/App.HostedCore`](Examples/App.HostedCore/README.md)
   Smallest hosted-runtime example. Uses `NGIN.Core` with code-first startup and
   staged config.
3. [`Examples/App.Basic`](Examples/App.Basic/README.md)
   Compact hosted application with project-owned runtime metadata.
4. [`Examples/App.Showcase`](Examples/App.Showcase/README.md)
   Richer configuration overlays, optional package references, and runtime
   module variation.
5. `Examples/Game.Engine`, `Examples/Game.Client`, and `Examples/Game.Server`
   Separate buildable outputs modeled as separate projects.

See [`Examples/README.md`](Examples/README.md) for the full example map.

## Why NGIN Exists

In many native codebases, build dependencies, runtime wiring, startup order,
environment selection, staged files, and reusable feature boundaries gradually
blur together. That makes simple questions harder than they should be:

- what is the buildable unit here?
- which features belong to this app, and which belong to reusable packages?
- which configuration decides the runtime shape?
- what files are supposed to be in the runnable output?

NGIN makes those answers explicit in authored manifests:

- `.ngin` for an optional workspace
- `.nginproj` for a buildable project
- `.nginpkg` for a reusable package
- `.nginlaunch` for generated staged launch metadata

The result is a workflow where the same authored model can be validated,
graphed, built, staged, launched, and consumed by editor tooling.

## Core Model

### Workspace

A workspace is an optional repo-level container. It lists projects, package
source roots, and local package providers. Single-project authoring does not
require a workspace file, but this umbrella repo uses [`NGIN.ngin`](NGIN.ngin) to
tie examples, package wrappers, and local dependency trees together.

### Project

A project is the primary buildable unit. It usually maps to one application,
tool, or library with its own source roots, output, references, and named
configurations.

If you have separate executables, they should usually be separate projects. For
example, a game client and a headless server are different projects. A shared
engine can be another local project or a package, depending on whether it is
still a local build unit or a reusable dependency.

### Configuration

A configuration is one named setup of the same project. It can select build
configuration, operating system, architecture, environment, launch details, and
narrow overlays for references, config, modules, or plugins.

Configurations are not meant to invent extra application identities. They are
for differences in setup on the same project.

### Package

A package is the reusable unit. Packages can expose libraries, executables,
modules, plugins, staged content, and bootstrap behavior. Workspace-local source
overrides are described by workspace `PackageProviders`, not by package-local
source binding metadata.

As a practical rule, keep something as a project while it is owned primarily by
one repo as a local buildable unit. Make it a package when it needs reusable
identity, dependency-style consumption, and independent versioning across
projects.

## Minimal Project Manifest

This is the core shape of a generated-build application project:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="MyApp"
         Type="Application"
         DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>

  <Output Kind="Executable"
          Name="MyApp"
          Target="MyApp" />

  <Build Backend="CMake"
         Mode="Generated"
         Language="CXX"
         LanguageStandard="23" />

  <Environments>
    <Environment Name="local" />
  </Environments>

  <Configurations>
    <Configuration Name="Runtime"
                   BuildConfiguration="Debug"
                   OperatingSystem="linux"
                   Architecture="x64"
                   Environment="local">
      <Launch Executable="MyApp" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
```

That says:

- `MyApp` is the buildable application.
- source files live under `src`.
- CMake backend input is generated from the manifest.
- `Runtime` is one launch/build setup of the same project.
- build output can be staged and launched through the CLI.

## CLI Surface

Project commands:

- `ngin validate [--project <file>] [--configuration <name>]`
- `ngin graph [--project <file>] [--configuration <name>]`
- `ngin build [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin run [--project <file>] [--configuration <name>] [--output <dir>] [-- <args...>]`
- `ngin clean [--project <file>] [--configuration <name>] [--output <dir>]`
- `ngin rebuild [--project <file>] [--configuration <name>] [--output <dir>]`

Workspace and package inspection:

- `ngin workspace list`
- `ngin workspace status`
- `ngin workspace doctor`
- `ngin package list`
- `ngin package show <Package>`

For the exact command contract, see
[`docs/specs/006-cli-contract.md`](docs/specs/006-cli-contract.md).

## Repository Map

- [`Tools/NGIN.CLI`](Tools/NGIN.CLI/) native `ngin` CLI implementation
- [`Tools/NGIN.VSCode`](Tools/NGIN.VSCode/) in-repo VS Code extension
- [`Tools/README.md`](Tools/README.md) contributor-facing tooling guide
- [`Examples`](Examples/) canonical authored examples
- [`Packages`](Packages/) package wrappers and local platform packages
- [`Packages/NGIN.Core`](Packages/NGIN.Core/) optional hosted runtime package
- [`Dependencies`](Dependencies/) local first-party and third-party source trees
- [`docs`](docs/) specs, architecture notes, plans, and historical drafts

## Documentation Map

Start with:

- [`Examples/README.md`](Examples/README.md) for learning by running projects
- [`Tools/README.md`](Tools/README.md) for CLI, VS Code, and backend tool details
- [`docs/README.md`](docs/README.md) for specs and deeper design references

The active contracts live in [`docs/specs`](docs/specs/). The most useful
starting specs are:

- [`001-core-concepts.md`](docs/specs/001-core-concepts.md)
- [`002-project-and-target-manifest.md`](docs/specs/002-project-and-target-manifest.md)
- [`003-package-manifest-and-runtime-contributions.md`](docs/specs/003-package-manifest-and-runtime-contributions.md)
- [`006-cli-contract.md`](docs/specs/006-cli-contract.md)
- [`010-workspace-and-project-model.md`](docs/specs/010-workspace-and-project-model.md)
- [`012-tooling-and-runtime-boundary.md`](docs/specs/012-tooling-and-runtime-boundary.md)

Architecture notes and older drafts are useful background, but the specs are the
source of truth for active behavior.
