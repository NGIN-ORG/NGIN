# Package manifest reference

A `.nginpkg` describes one reusable package:

```xml
<Package SchemaVersion="4"
         Name="Math"
         Version="1.0.0">
  <Build Backend="CMake" Mode="AddSubdirectory" />
  <Library Name="Math">
    <Exports>
      <LibraryTarget Name="Math::Math" />
    </Exports>
  </Library>
</Package>
```

`SchemaVersion`, `Name`, and `Version` identify the package. Compatibility
attributes and sections can constrain the NGIN platform, host, operating
system, architecture, or toolchain that may select it.

Validate exact syntax with the current CLI:

```bash
ngin schema --format json
ngin package show Math
```

## Common sections

| Section | Purpose |
| --- | --- |
| `Build` | Backend, integration mode, source location, and backend options |
| `Uses` | Package, tool, and runtime dependencies |
| `Library` | Library exports such as CMake targets, binaries, and headers |
| `Tool` | Host or development-tool exports |
| `ToolDrivers` | Protocol adapters for semantic tool execution |
| `ToolActions` | Analyze, format, scan, transform, report, or custom actions |
| `Features` | Opt-in dependency, build, runtime, generator, and tooling contributions |
| `Compatibility` | Supported hosts and targets |

## Build modes

`Build` currently supports CMake-backed `AddSubdirectory`, `FindPackage`, and
`Manual` modes. A workspace `PackageProvider` supplies the source root for
source-backed packages.

## Dependencies and scopes

Package dependencies use `Package`, `Tool`, or `Runtime` entries under `Uses`.
Scopes are `Build`, `Target`, `Runtime`, `Test`, `Dev`, and `Publish`; multiple
scopes are separated with semicolons.

## Features

A feature is selected by name from a project dependency:

```xml
<Package Name="NGIN.Reflection.MetaGen" Scope="Build">
  <Feature Name="ReflectionCodegen" />
</Package>
```

Features may contribute capabilities, dependencies, build settings,
generators, runtime data, and tooling. Contributions retain package and feature
provenance in the Composition Graph.

## Distribution

`ngin package pack` creates a package manifest or `.nginpack` archive.
`ngin restore` resolves package sources into the local package store, and
`ngin package lock` records the selected closure in `ngin.lock`.

Use the wrappers under [`Packages/`](../../Packages) as executable examples.
