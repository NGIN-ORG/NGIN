# NGIN

NGIN is a C++ project toolchain and optional application runtime.

It has two main parts:

1. **The `ngin` CLI**
   A command-line tool for describing, validating, building, packaging, staging,
   and running C++ projects.

2. **`NGIN.Core`**
   An optional C++ runtime library for applications that want a structured
   startup model with services, profile, modules, plugins, lifecycle hooks,
   reflection, and diagnostics.

You can use the CLI without using `NGIN.Core`.

That means NGIN can be used in two ways:

- as a build and project workflow for normal C++ applications
- as a fuller application framework when you also link `NGIN.Core`

NGIN does not replace CMake. CMake is currently the build backend. NGIN sits
above it and generates backend input from project metadata.

> NGIN is unrelated to the nginx web server.

---

## Why NGIN Exists

C++ projects often start simple:

```text
MyTool/
  CMakeLists.txt
  src/main.cpp
```

For a small project, that may be enough.

But larger native projects tend to grow beyond the build file. The actual shape
of the application gets spread across many places:

```text
MyGame/
  CMakeLists.txt          build targets, include paths, link rules
  config/dev.json         runtime profile
  scripts/stage.sh        copy files into a runnable folder
  scripts/run-dev.sh      local launch command
  packages/               reusable libraries or feature bundles
  README.md               notes about what has to be copied where
```

After a while, simple questions become hard to answer:

- What applications exist in this repository?
- What does each application depend on?
- Which profile am I building?
- Which files should be copied next to the executable?
- What does the local run command actually run?
- Which parts are project-specific, and which parts are reusable packages?

NGIN exists to put those answers in one explicit model.

---

## The Short Version

With NGIN, a project is described by a `.nginproj` file.

That file can describe things like:

- source folders
- output type, such as an executable or library
- dependencies on other projects or packages
- build backend settings
- named profiles
- files that should be copied into the runnable output folder
- local run settings

The `ngin` CLI can then use that project file to:

```text
validate  -> check that the project makes sense
graph     -> show what the project resolves to
build     -> generate backend input and build the output
stage     -> create a runnable folder
run       -> run the staged application locally
```

A **staged folder** is the folder you actually run from. It contains the built
executable plus the files it needs locally, such as config files, content files,
package outputs, copied libraries, and generated launch metadata.

---

## The Developer Workflow

NGIN is built around a direct project workflow:

- one project file describes the project
- a CLI understands that project file
- projects can reference other projects
- packages can contribute reusable functionality
- build, run, inspect, and package workflows go through one tool

For C++, CMake still does the actual backend build work. NGIN provides the
higher-level project model around it.

---

## The Two Parts Of NGIN

### 1. The `ngin` CLI

The CLI is the first thing most users should learn.

It is responsible for the project workflow:

- reading `.nginproj`, `.nginpkg`, and workspace files
- resolving project and package references
- validating project profiles
- generating CMake input
- configuring generated build metadata
- building projects
- creating runnable output folders
- generating local launch metadata
- running staged applications
- inspecting workspaces and packages

A plain C++ project can use this layer without linking any NGIN runtime library.

### 2. `NGIN.Core`

`NGIN.Core` is a C++ library that applications may choose to link.

It is for applications that want a managed startup structure instead of writing
all startup wiring manually.

It can provide:

- application lifecycle
- service registration
- profile loading
- modules
- plugins
- reflection
- diagnostics
- structured startup and shutdown

This is sometimes called a **hosted runtime** in the NGIN documentation.

In plain language, that means:

> Your `main()` starts an NGIN application host, and the host coordinates app
> startup, services, modules, profile, and shutdown.

You do not need `NGIN.Core` for a normal executable. It is only for applications
that want that structured runtime model.

---

## When To Use Only The CLI

Use the CLI-only path when you want NGIN to manage the project shape, build,
staging, and local run workflow, but your application still has a normal C++
entry point.

This is useful for:

- command-line tools
- simple applications
- native utilities
- game clients or servers with their own startup code
- projects that want generated CMake input
- projects that need predictable staged output folders
- repositories with multiple related C++ projects

In this mode, NGIN is mostly a project and build workflow.

---

## When To Use `NGIN.Core`

Use `NGIN.Core` when the application itself should use NGIN’s runtime model.

This is useful when your app needs:

- service-based architecture
- modules that can be registered and started consistently
- plugin loading
- shared profile infrastructure
- lifecycle hooks
- diagnostics and reflection support
- a common startup pattern across multiple applications

In this mode, NGIN is both:

- the project/build/staging toolchain
- the runtime structure of the application

---

## When Not To Use NGIN

You probably do not need NGIN if your project is still comfortable as:

- one executable
- a few source files
- minimal dependencies
- no copied runtime files
- no local staging step
- one straightforward `CMakeLists.txt`

That is fine. NGIN is meant for the point where C++ project structure starts
spreading beyond the build file.

---

## Quick Start

This quick start builds the `ngin` CLI from this repository and runs the
smallest example project.

The smallest example is:

```text
Examples/App.NativeMinimal/
  App.NativeMinimal.nginproj
  src/main.cpp
```

It is a normal C++ executable managed by the NGIN CLI.

### Prerequisites

You need:

- CMake 3.20 or newer
- Ninja
- a C++23-capable compiler
- a normal C++ development environment for your platform

### 1. Configure the repository

```bash
cmake --preset dev
```

### 2. Build the NGIN CLI

```bash
cmake --build build/dev --target ngin_cli
```

### 3. Check the workspace

```bash
./build/dev/Tools/NGIN.CLI/ngin workspace doctor
```

### 4. Validate the example project

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime
```

### 5. Inspect the resolved project

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime
```

### 6. Configure generated build metadata

```bash
./build/dev/Tools/NGIN.CLI/ngin configure \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime \
  --output build/manual/App.NativeMinimal
```

This generates backend CMake input and compile commands without building or
staging the application.

### 7. Build and stage the example

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime \
  --output build/manual/App.NativeMinimal
```

This builds the executable and creates a runnable output folder.

### 8. Run the staged example

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime \
  --output build/manual/App.NativeMinimal
```

The core flow is:

```text
.nginproj
  -> validate the project
  -> inspect the resolved structure
  -> generate CMake input
  -> configure build metadata
  -> build the executable
  -> create a runnable folder
  -> run it locally
```

---

## What A Minimal Project Manifest Looks Like

A small `.nginproj` file looks like this:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3"
         Name="MyApp"
         Type="Application"
         DefaultProfile="Runtime">
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>

  <Conditions>
    <Condition Name="LocalDebug"
               Environment="local"
               BuildType="Debug" />
  </Conditions>

  <Output Kind="Executable"
          Name="MyApp"
          Target="MyApp" />

  <Build Backend="CMake"
         Mode="Generated"
         Language="CXX"
         LanguageStandard="23">
    <CompileDefinitions>
      <Definition Value="MYAPP_LOCAL_DEBUG"
                  Visibility="Private"
                  Condition="LocalDebug" />
    </CompileDefinitions>
  </Build>

  <Environments>
    <Environment Name="local" />
  </Environments>

  <Profiles>
    <Profile Name="Runtime"
                   BuildType="Debug"
                   OperatingSystem="linux"
                   Architecture="x64"
                   Environment="local">
      <Launch Executable="MyApp" WorkingDirectory="." />
    </Profile>
  </Profiles>
</Project>
```

This says:

- the project is named `MyApp`
- source files are under `src`
- `LocalDebug` names the local debug selection rule
- the output is an executable
- CMake input should be generated
- the default profile is `Runtime`
- the `Runtime` profile builds a debug Linux x64 executable
- `MYAPP_LOCAL_DEBUG` is emitted only when `LocalDebug` matches
- local runs should launch `MyApp` from the staged folder

---

## Main File Types

NGIN uses a few manifest files.

### `.nginproj`

Describes one project.

A project is usually one app, tool, or library.

Examples:

```text
Game.Client.nginproj
Game.Server.nginproj
AssetCompiler.nginproj
```

### `.nginpkg`

Describes a reusable package.

A package can provide reusable libraries, tools, modules, plugins, config,
content, or other files that projects consume.

### `.ngin`

Describes a workspace.

A workspace is an optional repository-level file that ties together multiple
projects, package roots, and local package providers.

This repository uses:

```text
NGIN.ngin
```

### `.nginlaunch`

Describes how to run a staged output locally.

This file is generated by NGIN during build/staging. It is local tooling
metadata, not something you normally write by hand.

---

## Main Concepts

### Project

A project is one thing you can build.

That usually means:

- an application
- a command-line tool
- a library
- a game client
- a game server
- an editor
- an asset pipeline tool

If two outputs have different identities, they should usually be separate
projects.

For example:

```text
Game.Engine
Game.Client
Game.Server
```

### Profile

A profile is one named way to build or run the same project.

Examples:

```text
Runtime
Debug
Release
Editor
Server
Local
Shipping
```

Profiles are useful when the same project needs different settings for
different situations.

A profile can select things like:

- build type
- operating system
- architecture
- environment
- launch settings
- extra references
- config overlays
- runtime modules or plugins

### Package

A package is reusable functionality.

Use a package when something should be consumed dependency-style by one or more
projects.

A package may contribute:

- libraries
- executables
- headers
- config files
- content files
- modules
- plugins
- startup behavior

As a practical rule:

- keep something as a project while it mainly belongs to one repository
- make it a package when it needs reusable identity and independent versioning

### Workspace

A workspace groups projects and package locations for a repository.

Single-project usage does not require a workspace, but workspaces are useful for
larger repositories with multiple apps, tools, packages, and local dependencies.

---

## Example Learning Path

The examples are arranged so each one adds one idea.

1. [`Examples/App.NativeMinimal`](Examples/App.NativeMinimal/README.md)
   Smallest CLI-managed C++ executable.

2. [`Examples/App.HostedCore`](Examples/App.HostedCore/README.md)
   Smallest application that links `NGIN.Core` and uses the NGIN application
   host.

3. [`Examples/App.Basic`](Examples/App.Basic/README.md)
   Compact hosted application with project-owned runtime metadata.

4. [`Examples/App.Showcase`](Examples/App.Showcase/README.md)
   Richer profile overlays, optional package references, and runtime
   module variation.

5. `Examples/Game.Engine`, `Examples/Game.Client`, and `Examples/Game.Server`
   Multiple related build outputs modeled as separate projects.

See [`Examples/README.md`](Examples/README.md) for the full example map.

---

## CLI Commands

### Project commands

```text
ngin validate [--project <file>] [--profile <name>]
ngin graph    [--project <file>] [--profile <name>]
ngin configure [--project <file>] [--profile <name>] [--output <dir>]
ngin build    [--project <file>] [--profile <name>] [--output <dir>]
ngin run      [--project <file>] [--profile <name>] [--output <dir>] [-- <args...>]
ngin clean    [--project <file>] [--profile <name>] [--output <dir>]
ngin rebuild  [--project <file>] [--profile <name>] [--output <dir>]
```

### Workspace commands

```text
ngin workspace list
ngin workspace status
ngin workspace doctor
```

### Package commands

```text
ngin package list
ngin package show <Package>
```

For the exact command contract, see
[`docs/specs/006-cli-contract.md`](docs/specs/006-cli-contract.md).

---

## How NGIN Compares

| Tool        | Main focus                                                                          |
| ----------- | ----------------------------------------------------------------------------------- |
| CMake       | Describing and generating native build systems                                      |
| NGIN CLI    | Project model, CMake generation, package/project references, staging, and local run |
| `NGIN.Core` | Optional C++ application host and runtime services                                  |
| Premake     | Scripted generation of build files                                                  |
| Bazel       | Large-scale hermetic build graph and caching                                        |

NGIN does not try to replace all of these tools.

The intended relationship with CMake is:

```text
NGIN describes the C++ project at the application level.
CMake performs the backend build.
```

---

## Repository Map

```text
Tools/
  NGIN.CLI/          native ngin CLI implementation
  NGIN.VSCode/       in-repository VS Code extension

Examples/            example projects and learning path

Packages/
  NGIN.Core/         optional application runtime package

Dependencies/        local first-party and third-party dependency source trees

docs/                specs, architecture notes, plans, and historical drafts
```

Useful entry points:

- [`Tools/NGIN.CLI`](Tools/NGIN.CLI/)
- [`Tools/NGIN.VSCode`](Tools/NGIN.VSCode/)
- [`Tools/README.md`](Tools/README.md)
- [`Examples`](Examples/)
- [`Packages`](Packages/)
- [`Packages/NGIN.Core`](Packages/NGIN.Core/)
- [`Dependencies`](Dependencies/)
- [`docs`](docs/)

---

## Documentation Map

Start with:

- [`Examples/README.md`](Examples/README.md) for learning by running projects
- [`Tools/README.md`](Tools/README.md) for CLI, VS Code, and backend tool details
- [`docs/README.md`](docs/README.md) for deeper design references

The active contracts live in [`docs/specs`](docs/specs/).

Useful starting specs:

- [`001-core-concepts.md`](docs/specs/001-core-concepts.md)
- [`002-project-and-target-manifest.md`](docs/specs/002-project-and-target-manifest.md)
- [`003-package-manifest-and-runtime-contributions.md`](docs/specs/003-package-manifest-and-runtime-contributions.md)
- [`006-cli-contract.md`](docs/specs/006-cli-contract.md)
- [`010-workspace-and-project-model.md`](docs/specs/010-workspace-and-project-model.md)
- [`012-tooling-and-runtime-boundary.md`](docs/specs/012-tooling-and-runtime-boundary.md)

Architecture notes and older drafts are useful background, but the specs are the
source of truth for active behavior.
