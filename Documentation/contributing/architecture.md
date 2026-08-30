---
title: NGIN architecture boundaries
description: Understand authored manifests, semantic resolution, generated outputs, packages, source trees, and runtime libraries.
---

# NGIN architecture boundaries

NGIN combines a project system with optional application libraries. The
project system and runtime libraries are related but deliberately separate.

## Product flow

```text
authored manifests
       │
Manifest IR and semantic resolution
       │
Composition Graph
       ├──► CMake adapter ─► native compiler
       ├──► stage/run/test/publish plans
       └──► editor and inspection protocols
```

## Authored versus generated

Projects, packages, workspaces, source, CMake compatibility inputs, and this
documentation are authored. Build directories, generated CMake, staged
layouts, generated reflection registration, and launch files are outputs.

## Package versus source ownership

`Packages/` owns exposure, provider wiring, metadata, and integration.
`Dependencies/NGIN/` owns first-party implementation. This prevents build
composition concerns from leaking into reusable library internals.

## Runtime boundary

The CLI can manage a normal C++ product that links no NGIN application library.
NGIN.Core is an optional host. NGIN.Reflection, ECS, UI, and Log remain
separately adoptable capabilities.

## Semantic authority

The Composition Graph owns resolved meaning. Generated CMake and the VS Code
extension consume it; neither is allowed to become a second manifest resolver.
