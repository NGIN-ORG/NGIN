---
title: The NGIN project system
description: Author products and resolve them into one inspectable model for the complete application lifecycle.
---

# The NGIN project system

The project system is usable on its own. It manages native products without
requiring `NGIN.Core` or another application library.

## The model

| Concept | Responsibility |
| --- | --- |
| [Project](./project-system/projects.md) | One executable or library product |
| [Package](./project-system/packages.md) | Reusable exports, tools, runtime files, and build integration |
| [Workspace](./project-system/workspaces.md) | Discovery, versions, profiles, capabilities, and trust policy |
| [Composition Graph](./project-system/composition-graph.md) | Fully resolved source of truth |
| [Stage and Run plans](./project-system/build-stage-run.md) | Materialized runtime layout and executable intent |

## Product-first authoring

A project begins directly with its product:

```xml
<Executable Name="Editor">
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
  <Stage>
    <Asset Include="assets/**" To="assets" />
  </Stage>
</Executable>
```

There is no generic `Project` wrapper and no separate `Module` product kind.
Reusable module behavior is modeled by packages and libraries; a loadable
native plugin is `Library Kind="Plugin"`.

## Lifecycle from one graph

```text
validate → restore → configure → build → stage → run
                                  ├───────→ test
                                  ├───────→ benchmark
                                  └───────→ publish
```

These are not isolated scripts. Each operation consumes the same resolved
product and selected build context.
