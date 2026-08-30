---
title: NGIN.ECS C++ API
description: Public world, entity, query, command, schedule, executor, simulation, and diagnostic symbols.
---

# NGIN.ECS C++ API

**Header:** `<NGIN/ECS/ECS.hpp>`  
**Namespace:** `NGIN::ECS`  
**Target:** `NGIN::ECS`  
**Source:** [Dependencies/NGIN/NGIN.ECS/include/NGIN/ECS](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.ECS/include/NGIN/ECS)

## World and entities

| Symbol | Header | Role |
| --- | --- | --- |
| `World` | `World.hpp` | Owns entity identities and archetype component storage |
| `EntityId` | `Entity.hpp` | Generational entity identity |
| `WorldStats` | `World.hpp` | Storage and entity statistics |
| `ComponentInfo` | `TypeRegistry.hpp` | Registered component metadata |

## Selection and structural change

`Query<Terms...>` selects components. Terms include `Read<T>`, `Write<T>`,
`Optional<T>`, `With<T>`, `Without<T>`, `Changed<T>`, and `Added<T>`.
`RowView`, `ChunkView`, and `QueryRunStats` expose iteration results.
`Commands` and `DeferredEntity` record structural changes for a safe flush
boundary.

## Scheduling and simulation

| Symbol | Role |
| --- | --- |
| `Schedule` | Owns systems, groups, declared access, constraints, and ordering |
| `Executor`, `SerialExecutor`, `ParallelExecutor`, `DeterministicParallelExecutor` | Execute a resolved schedule |
| `RunContext`, `FrameInfo`, `FixedStepInfo`, `RunResult` | Per-run context and outcome |
| `Resource<T>`, `Local<T>`, `EventReader<T>`, `EventWriter<T>` | System parameters |
| `Simulation`, `SimulationConfig`, `StepResult` | Frames, fixed steps, resources, events, and schedule pipelines |

Component pointers and references can be invalidated by structural changes.
During iteration, use `Commands`; do not mutate the world's structure directly.
Parallel safety is derived from declared read/write access.

See the [ECS API guide](../../../api/ecs.md) for query syntax, scheduling, and
diagnostics.

