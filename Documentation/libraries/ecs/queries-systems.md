---
title: Queries and systems
description: Select matching component combinations and declare system access through typed parameters.
---

# Queries and systems

Queries select entities by component shape. Systems apply behavior to the
matching rows.

## Typed access

```cpp
NGIN::ECS::Query<
    NGIN::ECS::Read<Position>,
    NGIN::ECS::Read<Velocity>> racers{world};
```

Read and write declarations are part of the scheduling contract. They allow the
runtime to detect conflicts and determine which work may execute concurrently.

## Systems

```cpp
auto move = schedule.Each(
    "Move",
    [](Position& position, const Velocity& velocity) {
        position.X += velocity.X;
    });
```

Mutable reference parameters declare writes; const references declare reads.
Give systems stable descriptive names so diagnostics and schedule inspection
remain understandable.

## Ordering

Declare semantic dependencies instead of relying on insertion order:

```cpp
schedule.After(move, accelerate);
```

The scheduler can preserve that dependency while still running unrelated,
non-conflicting work in parallel.
