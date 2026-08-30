# Queries

A query answers one question: **which entities match, and what may I do with
their components?**

For ordinary per-entity systems, start with `Schedule::Each`; its function
parameters create the query for you. Use an explicit `Query` for custom
iteration, optional components, chunk spans, or low-level world access.

## Query Terms

| Term | Meaning inside the query |
| --- | --- |
| `Read<T>` | require `T` and expose it read-only |
| `Write<T>` | require `T` and expose it mutably |
| `Optional<T>` | match either way and expose `const T*` |
| `With<T>` | require `T` without exposing it |
| `Without<T>` | exclude entities with `T` |
| `Added<T>` | keep rows where `T` was recently added |
| `Changed<T>` | keep rows where `T` was recently written |

The type list is both a filter and a permission set. Code cannot call
`row.Get<Health>()` unless the query declared access to `Health`.

## Iterate Rows

```cpp
Query<
    Write<Position>,
    Read<Velocity>,
    Optional<Health>,
    Without<Frozen>> moving {world};

moving.ForEach([](auto row) {
    Position& position = row.template Get<Position>();
    const Velocity& velocity = row.template Get<Velocity>();
    const Health* health = row.template TryGet<Health>();

    position.X += velocity.X;
    (void)health;
});
```

Useful row operations are:

- `row.Entity()` — the matching entity ID.
- `row.Get<T>()` — declared required read or write access.
- `row.TryGet<T>()` — declared optional read access.
- `row.MarkChanged<T>()` — report an external/interior mutation.

Acquiring a mutable component automatically marks it changed.

## Use Queries In Systems

The schedule supplies and caches a query parameter for you:

```cpp
schedule.System(
    "Count enemies",
    [](Query<With<Enemy>, Without<Dead>>& enemies) {
        NGIN::UIntSize count = 0;
        enemies.ForEach([&](auto) { ++count; });
    });
```

The query's declared access also tells the scheduler which systems can safely
run together.

## Iterate Chunks

Chunk iteration reduces callback overhead and exposes contiguous columns:

```cpp
Query<Write<Position>, Read<Velocity>> moving {world};

moving.ForEachChunk([](const auto& chunk) {
    auto positions = chunk.template WriteSpan<Position>();
    const auto velocities = chunk.template ReadSpan<Velocity>();

    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        positions[i].X += velocities[i].X;
    }
});
```

`WriteSpan<T>` marks the full yielded range changed. `Entities()` returns the
matching entity IDs. You can also use `Count`, `EntityAt`, `Get`, and `TryGet`
for indexed chunk access.

`Added` and `Changed` may select scattered rows, so change-filtered chunks do
not expose typed spans.

## Change Baselines

A direct query compares `Added` and `Changed` against
`world.PreviousChangeVersion()` by default. Supply a version explicitly when
you own the boundary:

```cpp
Query<Read<Health>, Changed<Health>> damaged {world, sinceVersion};
```

Scheduled systems automatically compare against their own last successful run.
See [Change detection](ChangeDetection.md) for the full model.

## Lifetime Rules

Rows, chunk views, pointers, references, and spans are valid only during their
callback. Do not retain them.

Structural changes are rejected while a query is active. Record
[commands](Commands.md) and let the schedule apply them after iteration.
Read-only queries may be nested; mutable queries may not.

Query plans cache archetypes and columns. New shapes and `World::Clear` refresh
them automatically—application code does not manage the cache.
