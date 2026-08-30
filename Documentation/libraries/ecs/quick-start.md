---
title: NGIN.ECS quick start
description: Spawn an entity, schedule a system, and advance one simulation frame.
---

# NGIN.ECS quick start

## Before you start

You need the NGIN CLI, a C++23 compiler, and a workspace that discovers
`NGIN.ECS`. Create `EcsDemo/EcsDemo.nginproj` and `EcsDemo/src/main.cpp`.

## Add the package

```xml
<Executable Name="EcsDemo">
  <Uses>
    <Package Name="NGIN.ECS" Version="0.4" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

## Create the simulation

```cpp
#include <NGIN/ECS/ECS.hpp>

struct Position { float x; };
struct Velocity { float x; };

int main() {
    using namespace NGIN::ECS;

    Simulation simulation{SimulationConfig{.FixedDeltaTime = 0.0}};
    const auto entity = simulation.Spawn(Position{}, Velocity{2.0f});

    simulation.Schedule(Update).Each(
        "Move",
        [](Position& position,
           const Velocity& velocity,
           const FrameInfo& frame) {
            position.x += velocity.x * static_cast<float>(frame.DeltaTime);
        });

    simulation.Step(FrameInfo{.DeltaTime = 1.0});
    return simulation.Get<Position>(entity).x == 2.0f ? 0 : 1;
}
```

The function parameter types declare write access to `Position` and read access
to `Velocity` and frame information.

## Run it

```bash
ngin validate --project EcsDemo/EcsDemo.nginproj --configuration Debug
ngin run --project EcsDemo/EcsDemo.nginproj --configuration Debug
```

Exit `0` confirms that one frame moved the entity from `0` to `2`. Exit `1`
means the system did not produce the expected component value.

## If it fails

- Ensure the schedule is the default `Update` schedule advanced by `Step`.
- A mutable component parameter declares write access; a `const` parameter
  declares read access. Incorrect access declarations can block schedule build.
- Do not add/remove components immediately while iterating a query; use
  `Commands` for structural changes.

Continue with [world and entities](./world-entities.md), then use the
[ECS C++ reference](../../reference/cpp/ecs/index.md) for query terms and schedule operations.
