# NGIN

NGIN is the umbrella workspace for a general C++ application platform.

The active platform model is intentionally small:

- `Project`: the application definition
- `Target`: one concrete buildable application variant
- `Package`: the reusable unit users reference
- `Module`: a runtime composition unit provided by packages
- `Plugin`: an optional extension provided by packages
- `Host`: the runtime container built from a resolved target

Packages are the main authored unit. Modules and plugins are lower-level runtime details.

## What This Repo Is For

- active platform architecture and specs
- platform release and composition metadata
- the native `ngin` CLI
- cross-repo workspace validation and staging

This repo is not a single monolithic SDK library. The current host implementation lives in `NGIN.Core`.

## Authored Files

The active authored manifest family is XML-based:

- project: `.nginproj`
- package: `.nginpkg`
- staged target layout: `.ngintarget`

Dynamic runtime discovery uses lower-level XML descriptors:

- dynamic module descriptor: `.module.xml`
- dynamic plugin module descriptor: `.plugin-module.xml`

The canonical workspace project is [NGIN.Workspace.nginproj](/home/berggrenmille/NGIN/manifests/NGIN.Workspace.nginproj).

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
./build/dev/ngin doctor
./build/dev/ngin status
```

4. Validate and inspect a target:

```bash
./build/dev/ngin validate --project manifests/NGIN.Workspace.nginproj --target NGIN.CoreSample
./build/dev/ngin graph --project manifests/NGIN.Workspace.nginproj --target NGIN.EditorSample
```

5. Build a staged target layout:

```bash
./build/dev/ngin build --project manifests/NGIN.Workspace.nginproj --target NGIN.CoreSample --output build/manual/NGIN.CoreSample
```

6. Run the standard workspace flow through CMake:

```bash
cmake --build build/dev --target ngin.workflow
```

## Repository Map

- `docs/architecture/`: active architecture and concept docs
- `docs/api-drafts/`: public API/application-model drafts
- `docs/examples/`: example C++ and manifest authoring
- `docs/specs/`: active platform specs
- `manifests/`: workspace metadata plus package/module/plugin catalogs
- `tools/`: native CLI source
- `cmake/`: workspace CMake integration helpers

## Read Next

- [NGIN Concepts](/home/berggrenmille/NGIN/docs/architecture/NGIN-Concepts.md)
- [NGIN Architecture](/home/berggrenmille/NGIN/docs/architecture/NGIN-Architecture.md)
- [NGIN.Core Application Model Draft](/home/berggrenmille/NGIN/docs/api-drafts/NGIN.Core-ApplicationModel.md)
- [Spec 002: Host and Builder Model](/home/berggrenmille/NGIN/docs/specs/002-runtime-kernel-design.md)
- [Spec 003: Package, Module, and Plugin Model](/home/berggrenmille/NGIN/docs/specs/003-plugin-abi-header-spec.md)
- [Spec 005: Roadmap](/home/berggrenmille/NGIN/docs/specs/005-implementation-roadmap.md)
