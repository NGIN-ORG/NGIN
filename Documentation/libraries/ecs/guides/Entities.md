# Entities And Components

An entity is a small ID. Components are the data attached to it.

```cpp
struct Position { float X; float Y; };
struct Health { int Value; };
struct Player {};

World world;
const EntityId hero = world.Spawn(
    Position {10.0f, 4.0f},
    Health {100},
    Player {});
```

There is no entity class to inherit from and no registration step. `hero` is
simply the handle for a row containing those three component types.

## Read And Write Components

Use `Has` or `TryGet` when absence is normal. Use `Get` when absence is a bug.

```cpp
if (world.Has<Health>(hero))
{
    const Health& health = world.Get<Health>(hero);
}

if (const Position* position = world.TryGet<Position>(hero))
{
    // Read position.
}

world.GetMutable<Health>(hero).Value -= 10;
world.Set<Position>(hero, Position {12.0f, 5.0f});
```

`GetMutable`, `TryGetMutable`, and `Set` mark the component as changed. You do
not need to report ordinary writes separately.

## Change An Entity's Shape

Adding or removing a component moves the entity to storage with a different
shape:

```cpp
world.Add<Poisoned>(hero, Poisoned {.DamagePerSecond = 2});
world.Remove<Poisoned>(hero);
world.Despawn(hero);
```

Use these operations directly when no query is active. Inside systems or query
callbacks, record [deferred commands](Commands.md) instead.

For many identical entities, `SpawnBatch` avoids repeating setup work:

```cpp
auto stars = world.SpawnBatch(
    10'000,
    Position {},
    Brightness {1.0f});
```

The component values are prototypes and must be copyable.

## Entity Lifetime

`EntityId` contains an index and a generation. Reusing a freed slot does not
make an old handle valid again.

```cpp
if (hero && world.IsAlive(hero))
{
    // Safe to use in this world.
}
```

Important rules:

- A default-constructed ID is `NullEntityId`.
- IDs belong to the world that created them.
- `Despawn` ignores an already-stale ID; required operations reject one.
- `Clear` destroys every entity and invalidates every existing ID.
- Packed ID values are diagnostic details, not save-game or network IDs.

## World Or Simulation?

`Simulation` owns a `World` and forwards the common operations: spawn, batch
spawn, despawn, has, get, and mutable get.

```cpp
Simulation game;
const EntityId hero = game.Spawn(Health {100});
game.GetMutable<Health>(hero).Value -= 10;
```

Use `game.GetWorld()` for lower-level operations such as `Add`, `Remove`,
`Set`, manual queries, and statistics.

`World::Stats()` reports live entities, archetypes, chunks, and estimated chunk
capacity bytes. It is useful for telemetry, not total process memory accounting.
