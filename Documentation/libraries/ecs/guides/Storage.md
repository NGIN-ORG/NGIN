# How Storage Works

You can use NGIN.ECS without knowing its storage internals. Read this page when
you are tuning data layout, investigating invalidation, or choosing component
boundaries.

## Archetypes And Chunks

Entities with the same set of component types share an archetype. Each
archetype stores rows in fixed-capacity chunks, with one contiguous column per
non-tag component.

For entities containing `Position`, `Velocity`, and the empty `Player` tag:

```text
entity IDs       E0 E1 E2 ...
Position         P0 P1 P2 ...
Velocity         V0 V1 V2 ...
Player           no data column
added versions   one column per component
changed versions one column per component
```

This structure-of-arrays layout lets a movement query stream through the two
columns it needs without loading unrelated component data.

Chunks target 64 KiB of entity, component, and version capacity. A row too
large for that target gets a one-row chunk.

## Structural Changes

Adding or removing a component moves an entity between archetypes. NGIN.ECS:

1. constructs the complete destination row;
2. publishes the new location; and
3. removes the old row.

Existing change versions travel with their components. A newly added component
gets the current added version.

Removal uses swap removal: the last row may move into the hole. Entity
locations and all component/version columns move together. Excess empty chunks
are removed, while each used archetype retains one empty chunk for fast reuse.

Reserve predictable capacity before a loading spike:

```cpp
world.ReserveEntities(250'000);
world.ReserveArchetype<Position, Velocity>(250'000);
```

Reserved chunks stay allocated after despawn so the next population can reuse
them. `World::Clear` releases all archetypes and reservations.

If destination construction throws, completed destination columns are rolled
back and the source row remains valid. The [component requirements](ComponentRequirements.md)
make relocation itself non-throwing.

## What Invalidates References?

Assume that spawn, batch spawn, despawn, add, remove, and clear can invalidate
all component pointers, references, rows, chunks, and spans.

In-place writes and `Set` do not change the row's structure, but query-derived
references are still callback-scoped and must not escape iteration.

## Query Plans

Queries cache matching archetypes and component-column positions. Creating a
new archetype or calling `World::Clear` advances the world's schema generation;
cached plans notice and refresh automatically.

Dense queries iterate physical rows directly. Typed chunk spans expose the
contiguous columns for high-throughput loops. See [Queries](Queries.md).

Types under `NGIN/ECS/Detail` implement this model. They may be installed
because public templates depend on them, but they are not public extension
points.
