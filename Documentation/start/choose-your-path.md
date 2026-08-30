---
title: Choose your path
description: Find the right documentation path for your role and immediate goal.
---

# Choose your path

NGIN serves several kinds of work. Start with the path closest to the outcome
you need today.

## Application developer

1. Complete [your first project](./first-project.md).
2. Learn [projects](../project-system/projects.md) and
   [packages](../project-system/packages.md).
3. Add [NGIN.Core](../libraries/core.md) only if the application needs hosting,
   services, modules, configuration, or lifecycle management.

## Library author

1. Learn the `Library` product kinds in [projects](../project-system/projects.md).
2. Understand package exports in [packages](../project-system/packages.md).
3. Use the narrowest [NGIN.Base](../libraries/base.md) facilities that fit the
   library.

## Game or simulation developer

Start with [NGIN.ECS](../libraries/ecs.md). Add
[NGIN.UI](../libraries/ui.md) for application interfaces and
[NGIN.Core](../libraries/core.md) when the program benefits from a hosted
module lifecycle.

## Tooling or editor developer

Read the [Composition Graph](../project-system/composition-graph.md), then the
[CLI](../tools/cli.md) and [VS Code integration](../tools/vscode.md). Tooling
should consume the CLI protocol rather than creating another resolver.

## NGIN contributor

Start with [architecture](../contributing/architecture.md), then enter the
component or tool documentation for the ownership boundary you intend to
change.
