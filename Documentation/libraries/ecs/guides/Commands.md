# Change Entities Safely With Commands

Queries rely on stable storage while they iterate. Spawning, despawning, adding,
or removing components could move that storage, so systems record those changes
in `Commands` and apply them at the next schedule barrier.

## The Common Pattern

```cpp
schedule.System(
    "Remove dead enemies",
    [](Query<With<Enemy>, Read<Health>>& enemies,
       Commands& commands) {
        enemies.ForEach([&](auto row) {
            if (row.template Get<Health>().Value <= 0)
            {
                commands.Despawn(row.Entity());
            }
        });
    });
```

The schedule owns the command buffer and flushes it after the system's stage.
Never flush it from inside the query callback.

## Available Operations

```cpp
commands.Despawn(entity);
commands.Add<Shield>(entity, Shield {50});
commands.Set<Position>(entity, Position {4.0f, 2.0f});
commands.Remove<Velocity>(entity);
```

Operations are applied in recording order.

## Spawn And Keep Working With The New Entity

The real `EntityId` does not exist until commands are applied. `Spawn` therefore
returns a temporary `DeferredEntity` token:

```cpp
const DeferredEntity spark = commands.Spawn(
    Position {4.0f, 2.0f},
    Lifetime {0.5f});

commands.Add<Color>(spark, Color::Gold);
commands.Set<Lifetime>(spark, Lifetime {1.0f});
```

The token belongs to that one command buffer. Do not store it, move it to a
different buffer, or use it after the buffer is cleared or flushed.

## Manual Use

Outside a scheduled system, you may own a buffer directly:

```cpp
Commands commands;
commands.Spawn(Position {});
commands.Flush(world);
```

`Clear()` discards pending work without touching the world. Whole-world clear
is intentionally not a deferred operation; call `World::Clear` when no query is
active or use an exclusive system.

## If A Command Fails

A flush has a completed-prefix guarantee:

- earlier operations stay applied;
- the failing operation and everything after it are discarded;
- all remaining payloads are destroyed exactly once;
- deferred tokens are invalidated;
- the exception continues to the caller; and
- the buffer can be reused.

This is not a transaction. If your game rule needs all-or-nothing behavior,
validate the inputs before recording the commands.

Move-only component payloads and over-aligned values are supported. Recursive
flushes and recording new commands while a flush is applying are rejected.
