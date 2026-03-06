# NGIN.Core Application Model Draft

Status: Active draft  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-07

## Purpose

This draft describes how `NGIN.Core` fits into the simplified platform model.

`NGIN.Core` is the current host implementation. The umbrella workspace owns the project/package metadata model and the native `ngin` CLI.

## Platform Model

- `Project`: top-level application definition in `.nginproj`
- `Target`: one concrete application variant
- `Package`: main reusable unit in `.nginpkg`
- `Module`: runtime composition unit provided by packages
- `Plugin`: optional extension provided by packages
- `Host`: runtime container built from the resolved target

## Builder Model

`ApplicationBuilder` remains the public bootstrap API.

Expected flow:

1. create a builder
2. load or construct project state
3. select a target
4. configure services, packages, modules, plugins, and config
5. build the host

Packages remain the primary composition input. Direct module/plugin toggles are advanced overrides.

## Manifest Direction

The active authored manifest family is XML-based:

- project: `.nginproj`
- package: `.nginpkg`
- staged target layout: `.ngintarget`

Example files:

- [ApplicationBuilder.Basic.cpp](/home/berggrenmille/NGIN/docs/examples/project-model/ApplicationBuilder.Basic.cpp)
- [NGIN.Samples.ApplicationModel.nginproj](/home/berggrenmille/NGIN/docs/examples/project-model/NGIN.Samples.ApplicationModel.nginproj)
- [NGIN.ECS.nginpkg](/home/berggrenmille/NGIN/docs/examples/project-model/NGIN.ECS.nginpkg)
- [PackageBootstrap.ECS.cpp](/home/berggrenmille/NGIN/docs/examples/project-model/PackageBootstrap.ECS.cpp)

## Package Bootstrap

Packages may still declare bootstrap metadata and register builder hooks. That remains a package concern, not a separate top-level workflow.

## Current Intent

The draft is intentionally smaller than earlier versions:

- no user-facing lockfile model
- no JSON-first manifest story
- no Python-first tooling story
- no claim that plugins replace packages as the main composition unit
