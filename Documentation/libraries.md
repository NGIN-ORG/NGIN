---
title: NGIN libraries
description: Choose a library, then learn the subsystem that matches the problem you need to solve.
---

# NGIN libraries

NGIN libraries are independently adoptable. Choose a library first. Each
library is then divided into subsystems such as Async, Networking, Memory, or
Serialization so you can learn one area without reading the entire platform.

## Foundation and application model

| Library | What it provides | Explore |
| --- | --- | --- |
| [NGIN.Base](./libraries/base.md) | Foundation plus Async, Execution, Memory, I/O, Networking, Serialization, Crypto, Text, Math, and Time | [Choose a Base subsystem](./libraries/base.md) |
| [NGIN.Core](./libraries/core.md) | Application hosting, services, modules, configuration, lifecycle, and plugins | [Explore Core](./libraries/core.md) |

## Domain libraries

| Library | What it provides | Explore |
| --- | --- | --- |
| [NGIN.Reflection](./libraries/reflection.md) | Explicit runtime type metadata and generated registration | [Explore Reflection](./libraries/reflection.md) |
| [NGIN.ECS](./libraries/ecs.md) | Entities, components, queries, systems, schedules, and simulation | [Explore ECS](./libraries/ecs.md) |
| [NGIN.UI](./libraries/ui.md) | Backend-neutral native interfaces, layout, controls, input, accessibility, and testing | [Explore UI](./libraries/ui.md) |
| [NGIN.Log](./libraries/log.md) | Structured records, formatting, sinks, and bounded asynchronous delivery | [Explore Log](./libraries/log.md) |

## Supporting packages

NGIN also provides smaller integration and tool packages rather than forcing
their behavior into the primary libraries. See [supporting packages](./libraries/supporting-packages.md)
for Benchmark, Diagnostics, UI backends, accessibility, hosting, MetaGen, and
tooling roles.

## How to use this section

Every library page has three kinds of documentation:

1. **Learn** explains the model from first use onward.
2. **Guides** solve a concrete task with complete code.
3. **API reference** documents exact public types, functions, signatures,
   ownership, preconditions, and source locations.

## Composition is optional

The libraries can work together without collapsing their responsibilities:

```text
NGIN.UI.Hosting ──► NGIN.Core ──► NGIN.Log
       │                 │
       ▼                 ▼
    NGIN.UI         NGIN.Base

NGIN.Reflection and NGIN.ECS can be adopted independently.
```

Package wrappers express concrete dependencies. This diagram communicates the
conceptual integration points, not a complete transitive link graph.

The raw Markdown bundle for a library is available at `/llms/<library>.txt`.
