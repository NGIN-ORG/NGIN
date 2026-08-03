# NGIN

**A project system and modular application toolkit for modern C++.**

NGIN gives native C++ projects one explicit model for building, composing,
staging, and running applications.

```bash
ngin build
ngin run
```

NGIN currently generates CMake projects and uses the native compiler toolchain
underneath. Use it as a project tool on its own, or add libraries such as
`NGIN.Core`, `NGIN.Reflection`, `NGIN.ECS`, and `NGIN.UI` when they fit your
application.

> [!WARNING]
> NGIN is experimental. Manifests, package contracts, and library APIs may
> change before the first stable release. It is not yet recommended for
> production projects.

## Why NGIN?

A C++ application usually needs more than a compiler command. Its project
knowledge often ends up split across CMake files, dependency scripts, code
generators, asset-copy commands, IDE settings, and launch notes.

NGIN puts that intent in a model that both people and tools can inspect:

```text
.nginproj -> ngin -> generated CMake -> compiler -> staged application
```

NGIN does not replace the compiler or hide the generated build. CMake is the
current backend, and its generated files remain available for inspection.

## Use only what you need

The project tooling and application libraries are separate.

| Component | Purpose |
| --- | --- |
| `ngin` | Validate, build, stage, inspect, run, test, and publish projects |
| `NGIN.Base` | Foundational types and low-level utilities |
| `NGIN.Core` | Optional application host with services, modules, and lifecycle management |
| `NGIN.Reflection` | Runtime reflection APIs |
| `NGIN.Reflection.MetaGen` | Clang-based reflection metadata generation |
| `NGIN.ECS` | Entity-component-system library |
| `NGIN.UI` | Backend-neutral application UI toolkit |
| `NGIN.UI.Backend.SDL3` | SDL3 windowing and rendering backend |
| `NGIN.UI.Hosting` | Integration between `NGIN.UI` and `NGIN.Core` |
| `NGIN.Tooling.*` | Package-provided tools such as Clang-Tidy and Clang-Format |
| `NGIN.VSCode` | VS Code integration backed by the `ngin` CLI |

A normal C++ executable can use `ngin` without linking an NGIN runtime library.

## Try NGIN

You need Git, CMake 3.20 or newer, Ninja, and a C++23-capable compiler. Clone
the repository with its submodules, then build the CLI:

```bash
git clone --recursive https://github.com/NGIN-ORG/NGIN.git
cd NGIN
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Build and run the smallest example:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug \
  --output build/hello

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug \
  --output build/hello
```

On Windows, the executable is
`build\dev\Tools\NGIN.CLI\ngin.exe`. Once it is on `PATH`, the normal loop is:

```bash
ngin validate
ngin build
ngin run
```

See [Installation](docs/getting-started/installation.md) for platform notes and
[Your first project](docs/getting-started/first-project.md) for a complete
walkthrough.

## A minimal project

```text
MyApp/
|-- MyApp.nginproj
`-- src/
    `-- main.cpp
```

`MyApp.nginproj`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="MyApp" DefaultProfile="Debug">
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

One `.nginproj` describes one primary product: an application, library, tool,
test, benchmark, plugin, module, or external product. Profiles select complete
behavior, not just a compiler preset. A workspace is optional.

## Examples

Start with the example closest to what you want to build:

| Example | Demonstrates |
| --- | --- |
| [Hello.Native](Examples/Hello.Native) | Plain C++ executable managed by the CLI |
| [Hello.Hosted](Examples/Hello.Hosted) | Application hosted by `NGIN.Core` |
| [Hello.Reflection](Examples/Hello.Reflection) | Reflection metadata generation |
| [Hello.ECS](Examples/Hello.ECS) | Entity-component-system integration |
| [Hello.Analyzer](Examples/Hello.Analyzer) | Package-provided Clang-Tidy execution |
| [Hello.Formatter](Examples/Hello.Formatter) | Package-provided formatting |
| [NGIN.UI.Gallery](Examples/NGIN.UI.Gallery) | Standalone UI gallery |
| [NGIN.UI.Gallery.Hosted](Examples/NGIN.UI.Gallery.Hosted) | UI hosted through `NGIN.Core` |

The [examples guide](Examples/README.md) gives the recommended learning path.

## Documentation

- [Getting started](docs/getting-started/installation.md)
- [Project authoring](docs/guides/projects.md)
- [Packages and workspaces](docs/guides/packages.md)
- [CLI reference](docs/reference/cli.md)
- [Application libraries](docs/libraries/README.md)
- [Contributing](docs/contributing/building-ngin.md)

The [documentation index](docs/README.md) separates tutorials, guides,
reference material, and repository architecture.

## Repository layout

```text
Tools/          CLI and editor tooling
Packages/       package wrappers and locally owned runtime packages
Dependencies/   first-party and third-party source trees
Examples/       runnable projects
docs/           user, reference, architecture, and contributor documentation
```

## Build and test the repository

```bash
cmake --preset dev
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

Individual libraries provide narrower build and test commands in their own
READMEs.

## License

NGIN is licensed under the [Apache License 2.0](LICENSE).
