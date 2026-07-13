Here is the full README rewritten around the “easy build system” story, using your old README as the source material.

# NGIN

A friendly C++ build system that turns project manifests into runnable apps.

NGIN is for C++ projects that want a simple build story:

```bash
ngin build
ngin run
```

That is the main idea.

You describe your project once in a `.nginproj` file. NGIN uses that description to build the project, prepare the runnable output folder, and run the app locally.

No pile of glue scripts.

No “remember to copy this folder next to the executable”.

No mystery run command hiding in an old README.

Just:

```bash
ngin build
ngin run
```

> NGIN is unrelated to the nginx web server. Same letters, different beast.

---

## What NGIN is

NGIN is a C++ build system, package-aware project toolchain, and optional application runtime.

It has two main parts:

### `ngin`

The command-line build tool.

This is the part most projects use.

It can build, stage, run, inspect, validate, and package C++ projects.

### `NGIN.Core`

An optional C++ runtime library.

Use it when your application wants a structured startup model with services, modules, plugins, lifecycle hooks, reflection, diagnostics, and profile loading.

You do not need `NGIN.Core` to use NGIN as a build system.

A normal C++ app can use the CLI only.

---

## The simple story

Most of the time, NGIN should feel boring in the best possible way:

```bash
ngin build
ngin run
```

`ngin build` builds your project and prepares a runnable output folder.

`ngin run` runs the staged application locally.

That is the workflow NGIN is built around.

Behind the scenes, NGIN can resolve packages, generate backend build files, copy runtime files, prepare launch metadata, and apply profiles.

But you do not need to think about all of that first.

You ask NGIN to build the app.

NGIN handles the project machinery.

---

## Why NGIN exists

C++ projects often start simple:

```text
MyTool/
  CMakeLists.txt
  src/main.cpp
```

For small projects, that can be enough.

But native projects tend to grow.

Eventually you may have:

```text
MyGame/
  CMakeLists.txt
  config/dev.json
  scripts/stage.sh
  scripts/run-dev.sh
  packages/
  tools/
  assets/
  README.md
```

At that point, the build is no longer just “compile these files”.

The real project shape is spread across:

* build files
* scripts
* copied config files
* package notes
* local run commands
* README instructions
* someone’s memory

NGIN exists to put those things back into one explicit project model.

A project should be able to answer:

* What am I building?
* Which sources belong to it?
* What packages does it use?
* Which profile is active?
* What needs to be copied next to the executable?
* How do I run it locally?

NGIN makes those answers part of the build system.

---

## How NGIN fits with CMake

NGIN currently uses CMake as its native build backend.

That means CMake still does the low-level compiler and linker work.

But for an NGIN project, the normal developer workflow goes through the `ngin` CLI.

```text
.nginproj
   -> ngin
      -> generated CMake
         -> Ninja / compiler / linker
            -> built output
               -> staged runnable app
```

In plain language:

* NGIN is the build system you use.
* CMake is currently the generated backend.
* Ninja, your compiler, and your linker still do the native build work.
* The project model lives in NGIN manifests.

NGIN is not trying to pretend compilers do not exist.

It is trying to make the project easier to build, package, stage, and run.

---

## Quick start

This quick start builds the `ngin` CLI from this repository and runs the smallest example project.

The example is a normal C++ executable:

```text
Examples/Hello.Native/
  Hello.Native.nginproj
  src/main.cpp
```

### Prerequisites

You need:

* CMake 3.20 or newer
* Ninja
* a C++23-capable compiler
* a normal C++ development environment for your platform

### 1. Configure the repository

```bash
cmake --preset dev
```

### 2. Build the NGIN CLI

```bash
cmake --build build/dev --target ngin_cli
```

### 3. Build the example

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug \
  --output build/manual/Hello.Native
```

This builds the project and creates a runnable staged output folder.

### 4. Run the example

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug \
  --output build/manual/Hello.Native
```

That is the core loop:

```bash
ngin build
ngin run
```

Everything else is there when you need it.

---

## What is a staged folder?

A staged folder is the folder your app actually runs from.

It contains the built executable plus whatever the app needs nearby.

For example:

* config files
* content files
* copied libraries
* package outputs
* generated launch metadata

Think of staging as packing a lunchbox for your app.

The executable goes in.

The config goes in.

The content files go in.

The libraries go in.

Then NGIN runs the app from that packed folder.

This makes local runs predictable, because the app runs from the same kind of layout every time.

---

## A tiny `.nginproj`

A small NGIN project file can look like this:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4"
         Name="MyApp"
         DefaultProfile="Debug">

  <Conditions>
    <Condition Name="LocalDebug">
      <When Environment="local"
            BuildType="Debug" />
    </Condition>
  </Conditions>

  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>

    <Launch Executable="$(OutputName)"
            WorkingDirectory="." />
  </Application>

  <Profile Name="Debug">
    <Defaults>
      <BuildType Name="Debug" />
      <TargetPlatform Name="linux-x64" />
      <Environment Name="local" />
    </Defaults>

    <Application>
      <Build>
        <Define Name="MYAPP_LOCAL_DEBUG"
                When="LocalDebug" />
      </Build>
    </Application>
  </Profile>
</Project>
```

This says:

* the project is named `MyApp`
* it builds an application
* source files live under `src`
* the default profile is `Debug`
* the `Debug` profile builds for `linux-x64`
* the build type is `Debug`
* the environment is `local`
* `MYAPP_LOCAL_DEBUG` is only emitted when the `LocalDebug` condition matches
* local runs launch the output executable from the staged folder

The goal is not to make XML exciting.

The goal is to make the project explicit.

---

## What can a project describe?

A `.nginproj` file can describe things like:

* source folders
* output type, such as executable or library
* dependencies on other projects
* dependencies on packages
* build profiles
* target platform
* build backend settings
* runtime files
* staged output
* local run settings

You do not need all of that for a small project.

Start with the basics.

Add the rest when the project grows teeth.

---

## Build profiles

A profile is a named way to build or run the same project.

Examples:

```text
Debug
Release
Debug.Asan
Debug.Diagnostics
Release.Shipping
Editor
Runtime
```

Profiles are useful when the same project needs different settings for different situations.

A profile can select things like:

* build type
* operating system
* architecture
* environment
* launch settings
* extra references
* config overlays
* runtime modules
* plugins

Think of a profile as a named mode for the project.

You can keep the command simple:

```bash
ngin build --profile Debug
```

or use a classic build configuration:

```bash
ngin build --configuration Release
```

---

## Packages

NGIN is package-aware.

A package is reusable functionality with an identity.

A package may contribute:

* libraries
* executables
* headers
* config files
* content files
* tools
* modules
* plugins
* startup behavior

Use a package when something should be consumed dependency-style by one or more projects.

As a practical rule:

* keep something as a project while it mainly belongs to one repository
* make it a package when it needs reusable identity, versioning, or distribution

The package commands are there when you need them:

```bash
ngin package list
ngin package show <Package>
ngin package lock
```

But you do not need to start there.

Start with:

```bash
ngin build
```

---

## Workspaces

A workspace groups projects and package locations for a repository.

Single-project usage does not require a workspace.

Workspaces become useful when a repository contains multiple apps, tools, packages, and local dependencies.

A workspace can answer questions like:

* Which projects exist in this repository?
* Where are local packages found?
* Which package providers are available?
* Is the workspace configured correctly?

Useful workspace commands:

```bash
ngin workspace list
ngin workspace status
ngin workspace doctor
```

Again, these are support tools.

The main workflow is still:

```bash
ngin build
ngin run
```

---

## Optional runtime: `NGIN.Core`

NGIN can be just a build system.

That is the default mental model.

But applications can also choose to link `NGIN.Core`.

`NGIN.Core` is a C++ runtime library for applications that want a managed startup structure instead of wiring everything manually.

It can provide:

* application lifecycle
* service registration
* profile loading
* modules
* plugins
* reflection
* diagnostics
* structured startup and shutdown

In plain language:

> Your `main()` starts an NGIN application host, and the host coordinates startup, services, modules, profiles, plugins, diagnostics, and shutdown.

Use `NGIN.Core` when your app wants that structure.

Do not use it when you just want a normal executable.

NGIN the build system works either way.

---

## When to use NGIN

NGIN is useful when your project has started collecting build-adjacent stuff.

For example:

* multiple applications or tools
* reusable libraries
* local packages
* generated backend build files
* runtime config files
* copied assets or content
* local run commands
* debug/release/shipping profiles
* platform-specific settings
* staged output folders
* plugins or modules
* tools that need to run as part of the project

It is especially useful when “how to build and run this” should be obvious from the project files instead of reconstructed from scripts and notes.

---

## When not to use NGIN

You probably do not need NGIN yet if your project is still happily living as:

* one executable
* a few source files
* minimal dependencies
* no copied runtime files
* no packages
* no staging step
* one straightforward `CMakeLists.txt`

That is completely fine.

NGIN is for the point where the project starts becoming a small ecosystem.

---

## Main file types

NGIN uses a few manifest files.

### `.nginproj`

Describes one project.

A project is usually one application, tool, or library.

Examples:

```text
Hello.Native.nginproj
Hello.Hosted.nginproj
AssetCompiler.nginproj
```

### `.nginpkg`

Describes a reusable package.

A package can provide reusable libraries, tools, modules, plugins, config, content, or other files that projects consume.

### `.ngin`

Describes a workspace.

A workspace is an optional repository-level file that ties together multiple projects, package roots, and local package providers.

This repository uses:

```text
NGIN.ngin
```

### `.nginlaunch`

Describes how to run a staged output locally.

This file is generated by NGIN during build/staging.

You normally do not write it by hand.

---

## Concepts in plain language

### Project

A project is one thing you can build.

Usually that means:

* an application
* a command-line tool
* a library
* a game client
* a game server
* an editor
* an asset pipeline tool

If two outputs have different identities, they should usually be separate projects.

For example:

```text
Game.Client
Game.Server
Game.Editor
AssetCompiler
```

### Profile

A profile is a named mode for a project.

Examples:

```text
Debug
Release
Debug.Asan
Release.Shipping
Editor
Runtime
```

Use profiles when the same project needs different settings in different situations.

### Package

A package is reusable project stuff with a name.

It can contribute code, tools, headers, config, content, modules, plugins, or startup behavior.

### Workspace

A workspace is the map of a bigger repository.

It ties projects and package locations together.

### Staged folder

A staged folder is the runnable app folder.

It is where the executable and its nearby runtime files live.

---

## Examples

The examples are arranged as a learning path.

Start small, then add one idea at a time.

1. [`Examples/Hello.Native`](Examples/Hello.Native/README.md)
   The smallest CLI-managed C++ executable.

2. [`Examples/Hello.Hosted`](Examples/Hello.Hosted/README.md)
   The smallest application that links `NGIN.Core`, registers a static module, and selects it from the manifest.

3. [`Examples/Hello.Reflection`](Examples/Hello.Reflection/README.md)
   Reflection code generation through `NGIN.Reflection.MetaGen`.

4. [`Examples/Hello.Analyzer`](Examples/Hello.Analyzer/README.md)
   A package-provided Analyze action and driver through `NGIN.Tooling.ClangTidy`.

See [`Examples/README.md`](Examples/README.md) for the full example map.

---

## Useful commands when you need more

You do not need all of these on day one.

Most of the time, start with:

```bash
ngin build
ngin run
```

When something needs inspecting, NGIN has more tools.

### Project commands

| Command          | Use it when                                    |
| ---------------- | ---------------------------------------------- |
| `ngin build`     | You want to build and stage the project        |
| `ngin run`       | You want to run the staged app                 |
| `ngin validate`  | You want to check that the project makes sense |
| `ngin graph`     | You want to see what NGIN resolved             |
| `ngin configure` | You only want generated backend files          |
| `ngin clean`     | You want to remove generated output            |
| `ngin rebuild`   | You want a clean build                         |

Full shape:

```text
ngin validate  [--project <file>] [--profile <name>] [--configuration <name>]
ngin graph     [--project <file>] [--profile <name>] [--configuration <name>]
ngin configure [--project <file>] [--profile <name>] [--configuration <name>] [--output <dir>]
ngin build     [--project <file>] [--profile <name>] [--configuration <name>] [--output <dir>]
ngin run       [--project <file>] [--profile <name>] [--configuration <name>] [--output <dir>] [-- <args...>]
ngin clean     [--project <file>] [--profile <name>] [--configuration <name>] [--output <dir>]
ngin rebuild   [--project <file>] [--profile <name>] [--configuration <name>] [--output <dir>]
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
ngin package lock [--project <file>] [--profile <name>] [--configuration <name>] [--output <ngin.lock>]
ngin package verify-lock [--project <file>] [--profile <name>] [--configuration <name>] [--lock <ngin.lock>]
ngin explain package-feature <Package> <Feature> [--project <file>] [--profile <name>]
```

For the exact command contract, see [`docs/specs/006-cli-contract.md`](docs/specs/006-cli-contract.md).

---

## How NGIN compares

| Tool        | Main focus                                                                       |
| ----------- | -------------------------------------------------------------------------------- |
| NGIN        | C++ build system, package-aware project model, staging, profiles, and local runs |
| CMake       | Native build generation backend                                                  |
| Ninja       | Fast low-level build executor                                                    |
| `NGIN.Core` | Optional C++ application host and runtime services                               |
| Premake     | Scripted generation of build files                                               |
| Bazel       | Large-scale hermetic build graph and caching                                     |

NGIN is not trying to be every tool at once.

The intended relationship is:

```text
NGIN describes the project.
CMake currently provides the generated native backend.
Ninja/compiler/linker build the actual binaries.
NGIN stages and runs the result.
```

---

## Repository map

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

* [`Tools/NGIN.CLI`](Tools/NGIN.CLI/)
* [`Tools/NGIN.VSCode`](Tools/NGIN.VSCode/)
* [`Tools/README.md`](Tools/README.md)
* [`Examples`](Examples/)
* [`Packages`](Packages/)
* [`Packages/NGIN.Core`](Packages/NGIN.Core/)
* [`Dependencies`](Dependencies/)
* [`docs`](docs/)

---

## Documentation

Start with:

* [`Examples/README.md`](Examples/README.md) for learning by running projects
* [`Tools/README.md`](Tools/README.md) for CLI, VS Code, and backend tool details
* [`docs/README.md`](docs/README.md) for deeper design references

The active contracts live in [`docs/specs`](docs/specs/).

Useful starting specs:

* [`001-core-concepts.md`](docs/specs/001-core-concepts.md)
* [`002-project-and-target-manifest.md`](docs/specs/002-project-and-target-manifest.md)
* [`003-package-manifest-and-runtime-contributions.md`](docs/specs/003-package-manifest-and-runtime-contributions.md)
* [`006-cli-contract.md`](docs/specs/006-cli-contract.md)
* [`010-workspace-and-project-model.md`](docs/specs/010-workspace-and-project-model.md)
* [`012-tooling-and-runtime-boundary.md`](docs/specs/012-tooling-and-runtime-boundary.md)

Architecture notes and older drafts are useful background, but the specs are the source of truth for active behavior.

---

## The short pitch

NGIN is a C++ build system where the main workflow is simple:

```bash
ngin build
ngin run
```

Packages, profiles, staging, generated CMake, launch metadata, and runtime files are handled by the project model.

You can inspect and customize those things when you need to.

But you do not need to start there.

Build the app.

Run the app.

Let NGIN carry the boxes.
