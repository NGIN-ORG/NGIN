---
title: Start with NGIN
description: Build your first native product or choose the shortest path to the NGIN capability you need.
---

# Start with NGIN

NGIN gives modern C++ applications one explicit project model and a set of
optional libraries. The project system can build an ordinary native executable
without linking an NGIN runtime. Libraries are adopted only when their model
solves a problem in your application.

## What do you want to do?

| I want to… | Go here |
| --- | --- |
| Build my first executable | [Follow the recommended path](#the-recommended-first-run) |
| Understand projects, packages, workspaces, and the Composition Graph | [Learn the project system](./project-system.md) |
| Add Async, Networking, Memory, Serialization, or another Base subsystem | [Explore NGIN.Base](./libraries/base.md) |
| Use application hosting, services, modules, or lifecycle | [Explore NGIN.Core](./libraries/core.md) |
| Choose Reflection, ECS, UI, or Log | [Browse all libraries](./libraries.md) |
| Look up a C++ declaration | [Open the C++ API reference](./reference/cpp/index.md) |
| Find an exact CLI or manifest contract | [Open Reference](./reference.md) |
| Contribute to NGIN | [Choose the contributor path](./start/choose-your-path.md#ngin-contributor) |

## The recommended first run

This path starts with a plain C++ executable. It introduces packages,
workspaces, and libraries only after the smallest product works.

```text
Install CLI → Create project → Validate → Build → Run → Inspect the graph
```

1. [Install and verify the CLI](./start/installation.md).
2. [Create, validate, build, and run your first project](./start/first-project.md).
3. [Understand how NGIN resolves the product](./start/mental-model.md).
4. Add [packages](./project-system/packages.md) or a
   [workspace](./project-system/workspaces.md) when the product needs them.
5. Choose an [NGIN library](./libraries.md) only when the application needs its
   capabilities.

After step two, you will have a normal native executable managed by NGIN but
not coupled to an NGIN runtime library.

## One model drives the lifecycle

NGIN records authored product intent, resolves it into a Composition Graph,
and derives the rest of the lifecycle from that graph. CMake remains the
current generated build backend, and the native compiler remains visible.

```text
authored manifests
       │
       ▼
Composition Graph ──► configure ──► build ──► stage ──► run
       │                              ├─────► test
       │                              └─────► benchmark
       └─────────────────────────────► inspect and editor tooling
```

When behavior is surprising, inspect the resolved graph before debugging
generated build files.

## Choose libraries by capability

| Need | Library |
| --- | --- |
| Async, execution, memory, containers, I/O, networking, serialization, crypto, text, math, or time | [NGIN.Base](./libraries/base.md) |
| Application host, services, modules, configuration, and lifecycle | [NGIN.Core](./libraries/core.md) |
| Explicit generated runtime type metadata | [NGIN.Reflection](./libraries/reflection.md) |
| Entity-component-system simulation | [NGIN.ECS](./libraries/ecs.md) |
| Backend-neutral native application interfaces | [NGIN.UI](./libraries/ui.md) |
| Structured records, sinks, formatting, and bounded asynchronous logging | [NGIN.Log](./libraries/log.md) |

Plain projects should remain plain. A library is an application capability,
not a requirement for using the NGIN project system.

## After the first project

- Learn the complete [project contract](./project-system/projects.md).
- Add reusable dependencies through [packages](./project-system/packages.md).
- Share discovery, versions, profiles, and policy through
  [workspaces](./project-system/workspaces.md).
- Inspect exact commands and manifest elements in [Reference](./reference.md).
- Enter [Libraries](./libraries.md) when you are ready to add application
  capabilities.
