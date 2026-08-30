# Change Detection Without Bookkeeping

Use change filters when a system only cares about data that is new since its
last successful run—for example, rebuilding transforms only after positions
move.

```cpp
update.System(
    "Rebuild matrices",
    [](Query<Read<Position>, Changed<Position>>& changed) {
        changed.ForEach([](auto row) {
            const Position& position = row.template Get<Position>();
            // Rebuild the derived matrix for this entity.
        });
    });
```

The scheduled system remembers its own last successful change version. If it
is skipped or fails, that baseline stays put, so changes are not accidentally
missed on its next run.

## What Counts As Changed?

- Spawn and `Add` mark a component as added.
- `Set`, `GetMutable`, and `TryGetMutable` mark it changed.
- Acquiring write access through a query row marks that row changed.
- Acquiring `WriteSpan<T>` marks every row in the span changed.
- `MarkChanged<T>` reports mutation performed through externally owned or
  interior state.

Marking happens when mutable access is acquired, not after NGIN.ECS compares
old and new values. This can produce a harmless false positive, but never a
silent false negative.

## Added Versus Changed

`Added<T>` selects components attached since the baseline. `Changed<T>` selects
mutable acquisitions since the baseline. Combine a filter with access when you
need the value:

```cpp
Query<Read<Health>, Added<Health>> newlyHealthy {world};
Query<Read<Health>, Changed<Health>> damaged {world};
```

Versions survive archetype moves and swap removal, so adding an unrelated
component does not make every existing component look new.

## Direct Queries

A query created directly from a world uses `PreviousChangeVersion()` as its
baseline:

```cpp
world.AdvanceChangeVersion();
world.GetMutable<Position>(ship).X += 1.0f;

Query<Read<Position>, Changed<Position>> moved {world};
```

Pass a version to the query constructor when you manage a different baseline.

`Simulation` advances change boundaries for startup, fixed steps, and variable
updates. In a manual loop, call `World::AdvanceChangeVersion()` yourself.
Unsigned version rollover is supported.
