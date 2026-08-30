---
title: NGIN.ECS
description: A C++23 entity-component-system library for games, simulations, and tools.
---

# NGIN.ECS

`NGIN.ECS` models application state as entities composed from plain component
types. System parameters declare which components each operation reads and
writes.

## Start here

1. Spawn and move an entity in the [quick start](./ecs/quick-start.md).
2. Learn [world and entity](./ecs/world-entities.md) ownership.
3. Express work through [queries and systems](./ecs/queries-systems.md).
4. Coordinate frames in [simulation and scheduling](./ecs/simulation-scheduling.md).
5. Keep the [ECS C++ reference](../reference/cpp/ecs/index.md) open while writing world,
   query, command, schedule, or simulation code.

## Model

| Type | Responsibility |
| --- | --- |
| `World` | Owns entities and components in archetype storage |
| `Query` | Selects matching component combinations |
| `Schedule` | Describes work, access, dependencies, and ordering |
| `Executor` | Runs compatible work |
| `Simulation` | Owns frames, fixed steps, resources, events, and schedules |

Parallel execution is opt in and derived from declared access. Low-level world,
query, schedule, and chunk APIs remain available.

Structural changes can invalidate component references. Record them in
`Commands` during query/system iteration and let the schedule flush at a safe
boundary. The [symbol index](../reference/cpp/ecs/index.md) lists the supported terms,
parameters, executors, and diagnostics.

## Detailed guides

The [NGIN.ECS guide library](./ecs/guides/index.md) goes deeper into entities,
component requirements, archetype storage, queries, commands, systems, change
detection, simulation, errors, and threading. Use it after the quick start or
jump directly to the topic you need.
