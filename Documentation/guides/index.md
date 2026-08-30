---
title: Guides
description: Task-focused instructions for building NGIN products and using NGIN libraries.
---

# Guides

Start with the outcome you need. Each guide names its prerequisites, gives you
something runnable, and tells you how to check that it worked.

## Build an NGIN product

| Goal | Guide | You will finish with |
| --- | --- | --- |
| Create an executable | [Your first project](../start/first-project.md) | A validated, built, runnable native product |
| Describe a product | [Projects](../project-system/projects.md) | A product-first `.nginproj` manifest |
| Consume a library | [Packages](../project-system/packages.md) | A resolved package dependency |
| Build and launch | [Build, stage, and run](../project-system/build-stage-run.md) | A staged runtime layout and launch plan |
| Work across products | [Workspaces](../project-system/workspaces.md) | A workspace that resolves projects and packages together |
| Explain resolution | [Composition Graph](../project-system/composition-graph.md) | Evidence for where build and runtime settings came from |

## Use a C++ library

| Library | Start here | Use it for |
| --- | --- | --- |
| NGIN.Base | [Base quick start](../libraries/base/quick-start.md) | Async, execution, memory, I/O, networking, serialization, crypto, text, and math |
| NGIN.Core | [Core quick start](../libraries/core/quick-start.md) | Hosted applications, services, modules, events, tasks, and plugins |
| NGIN.Reflection | [Reflection quick start](../libraries/reflection/quick-start.md) | Generated and runtime type metadata |
| NGIN.ECS | [ECS quick start](../libraries/ecs/quick-start.md) | Entity-component systems, queries, schedules, and simulation |
| NGIN.UI | [UI quick start](../libraries/ui/quick-start.md) | Retained UI trees, layout, input, rendering, and accessibility |
| NGIN.Log | [Logging quick start](../libraries/log/quick-start.md) | Structured records, formatters, sinks, and async delivery |

When you know the symbol you need, skip the guide and use the [API
C++ [API reference](../reference/cpp/index.md). When a command fails, go to
[troubleshooting](../troubleshooting/index.md).
