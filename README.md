# NGIN

NGIN is a C++ application composition and staging toolchain.

It exists because in many native codebases, the shape of the application gets
spread across build scripts, runtime code, config files, staged folders, and
tribal knowledge. What should be a clear answer to "what is this app?" turns
into archaeology.

NGIN makes that shape explicit.

A project manifest describes:

- what the buildable unit is
- what it depends on, including local projects and reusable packages
- how it is configured
- what a runnable output should contain

The same authored model can then be validated, inspected, built, staged, and
run without guessing how the pieces fit together.

NGIN separates what your application is from how it happens to be built and
staged.

## Who This Is For

NGIN is for teams and projects where:

- build logic, runtime wiring, package contents, and staging have started to
  blur together
- adding a feature means touching build scripts, config, and startup code in
  different places
- it is no longer obvious what ends up in the final runnable output
- multiple applications, tools, packages, or runtime modes need one inspectable
  source of truth

If your project still fits comfortably in a single handwritten `CMakeLists.txt`,
you probably do not need NGIN.

If it does not, that is where NGIN starts to pay off.

## What NGIN Is

NGIN has two separate layers:

- NGIN tooling: manifests, resolution, build generation, validation, graphing,
  staging, local run/debug, workspace inspection, and package workflows.
- `NGIN.Core`: an optional hosted runtime for services, configuration, modules,
  plugins, lifecycle, reflection, and diagnostics.

Plain C++ applications can use the tooling layer without linking `NGIN.Core`.
Applications link `NGIN.Core` only when they want the hosted startup model.

NGIN is not a replacement for CMake. It sits above it.

CMake remains the current build backend. NGIN generates the build input so
normal application projects can be described by `.nginproj` metadata instead of
hand-authored project `CMakeLists.txt` files.

## Quick Start

This checkout builds the native `ngin` CLI from source, then uses it to
validate, build, stage, and run an example application.

Prerequisites:

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

That flow proves the core path: a `.nginproj` describes a normal C++
executable, generates backend build input, produces a runnable staged directory,
and emits local launch metadata.

## From Manifest to Running App

`ngin validate` checks that the selected project configuration resolves to a
clear composition. A composition is the concrete application shape produced from
one project configuration plus its referenced projects, packages, config,
content, runtime metadata, and launch settings.

`ngin graph` prints the resolved structure so you can see what contributed to
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

Native applications are rarely just "some sources and a compiler invocation."
They usually have build dependencies, runtime wiring, startup order, environment
selection, staged files, reusable feature boundaries, and local launch rules.

Over time, those concerns inevitably blur together. That turns simple questions
into guesswork:

- what is the buildable unit here?
- which features belong to this app, and which belong to reusable packages?
- which configuration decides the runtime shape?
- what files are supposed to be in the runnable output?
- what does the tool actually run when I press debug?

NGIN makes those answers explicit in authored manifests:

- `.ngin` for an optional workspace
- `.nginproj` for a buildable project
- `.nginpkg` for a reusable package
- `.nginlaunch` for generated staged launch metadata

That gives native applications a single, inspectable source of truth for their
shape.

## Core Model

### Workspace

A workspace is an optional repo-level container. It lists projects, package
source roots, and local package providers. Single-project authoring does not
require a workspace file, but this repository uses [`NGIN.ngin`](NGIN.ngin) to
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
