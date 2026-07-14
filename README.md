# NGIN

**A modern project and build system for C++.**

NGIN gives C++ projects one place to describe how they are built, configured, staged, and run.

```bash
ngin build
ngin run
```

Instead of spreading project knowledge across CMake files, shell scripts, copied assets, launch commands, and README notes, NGIN keeps it in an explicit project model.

> [!IMPORTANT]
> NGIN is under active development and is not yet production-ready.

## Why NGIN?

Native projects often accumulate more than compiler settings:

* multiple applications and libraries
* build profiles and platform-specific configuration
* package and project dependencies
* generated source code
* runtime assets and configuration
* staging and launch scripts
* developer tooling

NGIN brings these pieces together without hiding the underlying native toolchain.

```text
.nginproj
    ↓
   NGIN
    ↓
generated CMake
    ↓
Ninja + compiler
    ↓
staged application
```

CMake remains the current native backend. NGIN provides the higher-level project model and developer workflow.

## Features

* Simple `build`, `run`, `validate`, and `clean` commands
* Declarative project manifests
* Debug, Release, sanitizer, shipping, and custom profiles
* Project and package dependency resolution
* Predictable staged application folders
* Runtime file and asset handling
* Generated CMake builds
* Multi-project workspaces
* Optional application runtime with modules and services
* Optional Clang-based reflection generation
* VS Code integration

## Requirements

To build NGIN:

* CMake 3.20 or newer
* Ninja
* A C++23-capable compiler

Supported development toolchains currently include recent versions of:

* Clang
* GCC
* MSVC

### Reflection requires Clang

The core NGIN build system does **not** require Clang.

However, `NGIN.Reflection.MetaGen` currently uses **LLVM/libclang** to parse C++ source and generate reflection metadata.

Without libclang:

* the NGIN CLI and normal native projects can still be built
* reflection generation is unavailable
* MetaGen is built as an unavailable stub

CMake attempts to discover Clang through `llvm-config`, `Clang_DIR`, or the following Windows overrides:

```text
LIBCLANG_INCLUDE_DIR
LIBCLANG_LIBRARY
```

## Quick start

Clone the repository:

```bash
git clone https://github.com/NGIN-ORG/NGIN.git
cd NGIN
```

Configure and build the CLI:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Build the smallest example:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug \
  --output build/hello
```

Run it:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug \
  --output build/hello
```

On Windows, use:

```powershell
.\build\dev\Tools\NGIN.CLI\ngin.exe
```

Once `ngin` is installed or added to `PATH`, the normal workflow is simply:

```bash
ngin build
ngin run
```

## A minimal project

```text
MyApp/
├── MyApp.nginproj
└── src/
    └── main.cpp
```

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4"
         Name="MyApp"
         DefaultProfile="Debug">

  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>

    <Launch Executable="$(OutputName)" />
  </Application>

  <Profile Name="Debug">
    <Defaults>
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
      <LinkTimeOptimization Enabled="false" />
      <TargetPlatform Name="host" />
    </Defaults>
  </Profile>

</Project>
```

Build it from the project directory:

```bash
ngin build
```

NGIN discovers the `.nginproj`, generates the backend build, compiles the project, and prepares a runnable staged folder.

`ngin new` creates `Debug`, `Release`, `RelWithDebInfo`, and `MinSizeRel`
profiles targeting the host platform. A profile describes behavior directly
through optimization, debug-symbol, LTO, and product build settings; NGIN
derives the backend configuration needed by CMake.

## Commands

```bash
ngin build
ngin run
ngin rebuild
ngin clean

ngin validate
ngin graph
ngin configure
```

For larger repositories:

```bash
ngin workspace list
ngin workspace status
ngin workspace doctor
```

For packages:

```bash
ngin package list
ngin package show <package>
ngin package lock
```

## Project model

NGIN uses four main file types:

| File          | Purpose                                               |
| ------------- | ----------------------------------------------------- |
| `.nginproj`   | Describes an application, library, or tool            |
| `.nginpkg`    | Describes a reusable package                          |
| `.ngin`       | Describes an optional multi-project workspace         |
| `.nginlaunch` | Generated launch information for a staged application |

Most projects begin with a single `.nginproj`.

## Optional runtime

NGIN can be used purely as a build system.

Projects may also use `NGIN.Core`, an optional application runtime providing:

* application lifecycle management
* service registration
* modules and plugins
* profile loading
* diagnostics
* structured startup and shutdown
* reflection integration

A normal C++ executable does not need `NGIN.Core`.

## Examples

Start with the smallest example and add features as needed:

* [`Hello.Native`](Examples/Hello.Native) — minimal CLI-managed executable
* [`Hello.Hosted`](Examples/Hello.Hosted) — application using `NGIN.Core`
* [`Hello.Reflection`](Examples/Hello.Reflection) — Clang-based reflection generation
* [`Hello.Analyzer`](Examples/Hello.Analyzer) — Clang-Tidy integration
* [`Hello.Formatter`](Examples/Hello.Formatter) — Clang-Format integration

See [`Examples/README.md`](Examples/README.md) for the complete learning path.

## Repository

```text
Tools/
  NGIN.CLI/                 command-line build tool
  NGIN.VSCode/              VS Code extension

Packages/
  NGIN.Base/                foundational C++ library
  NGIN.Core/                optional application runtime
  NGIN.Reflection.MetaGen/  reflection code generator

Examples/                   runnable example projects
docs/                       specifications and architecture
```

## Documentation

* [Examples](Examples/README.md)
* [Tools](Tools/README.md)
* [Documentation index](docs/README.md)
* [Core concepts](docs/specs/001-core-concepts.md)
* [Project manifest specification](docs/specs/002-project-and-target-manifest.md)
* [CLI contract](docs/specs/006-cli-contract.md)

## Status

NGIN is currently experimental.

The project model, manifests, CLI behavior, and package interfaces may change as development continues. Feedback, experiments, and contributions are welcome.

## License

See [`LICENSE`](LICENSE).
