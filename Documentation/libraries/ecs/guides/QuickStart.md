# Build Your First World

In this walkthrough, a ship moves across a tiny world. It takes two components,
one system, and one simulated frame.

## 1. Describe The Data

Components are plain C++ types. Keep each one focused on a single kind of data.

```cpp
struct Position
{
    float X;
    float Y;
};

struct Velocity
{
    float X;
    float Y;
};

struct Frozen {};
```

`Frozen` is an empty tag: its presence means something, but it stores no value.

## 2. Create A Simulation And An Entity

```cpp
using namespace NGIN::ECS;

Simulation game {SimulationConfig {.FixedDeltaTime = 0.0}};

const EntityId ship = game.Spawn(
    Position {0.0f, 0.0f},
    Velocity {6.0f, 2.0f});
```

An entity is only an ID. Its components give it shape and behavior.

We disable fixed updates for this first example. Every call to `Step` will run
the frame schedules once.

## 3. Add A System

```cpp
game.Schedule(Update).Each<Without<Frozen>>(
    "Move ships",
    [](Position& position,
       const Velocity& velocity,
       const FrameInfo& frame) {
        const float seconds = static_cast<float>(frame.DeltaTime);
        position.X += velocity.X * seconds;
        position.Y += velocity.Y * seconds;
    });
```

The function signature is the access declaration:

- `Position&` writes `Position`.
- `const Velocity&` reads `Velocity`.
- `Without<Frozen>` skips frozen entities.
- `FrameInfo` supplies the frame delta.

`Each` runs once for every matching entity.

## 4. Run A Frame

```cpp
(void)game.Step(FrameInfo {.DeltaTime = 1.0});

const Position& position = game.Get<Position>(ship);
// position is now {6, 2}
```

That is the normal NGIN.ECS loop: describe data, register systems once, then
call `Step` from your application loop.

## Complete Program

```cpp
#include <NGIN/ECS/ECS.hpp>

#include <iostream>

struct Position { float X; float Y; };
struct Velocity { float X; float Y; };
struct Frozen {};

int main()
{
    using namespace NGIN::ECS;

    Simulation game {SimulationConfig {.FixedDeltaTime = 0.0}};
    const EntityId ship = game.Spawn(Position {}, Velocity {6.0f, 2.0f});

    (void)game.Schedule(Update).Each<Without<Frozen>>(
        "Move ships",
        [](Position& position,
           const Velocity& velocity,
           const FrameInfo& frame) {
            const float seconds = static_cast<float>(frame.DeltaTime);
            position.X += velocity.X * seconds;
            position.Y += velocity.Y * seconds;
        });

    (void)game.Step(FrameInfo {.DeltaTime = 1.0});

    const auto& position = game.Get<Position>(ship);
    std::cout << position.X << ", " << position.Y << '\n';
}
```

Expected output:

```text
6, 2
```

## Where Next?

- Add game logic with [systems and scheduling](Systems.md).
- Learn how frame and fixed-step work fits together in [simulation and time](Simulation.md).
- Use [queries](Queries.md) when a system needs custom iteration.
- Use [commands](Commands.md) to spawn or reshape entities during a system.
