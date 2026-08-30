---
title: ConcurrentHashMap
description: Code reference for concurrent map operations, sharding, iteration, reclamation policies, and diagnostics.
---

# `ConcurrentHashMap`

**Header:** `<NGIN/Containers/ConcurrentHashMap.hpp>`  
**Namespace:** `NGIN::Containers`

```cpp
enum class ReclamationPolicy : std::uint8_t {
    ManualQuiesce,
    LocalEpoch,
    HazardPointers,
};

template<class Key, class Value, class Hash, class Equal,
         Memory::AllocatorConcept Alloc, ReclamationPolicy Policy,
         std::size_t ShardCount>
class ConcurrentHashMap;
```

## Operations

`Insert` and `InsertOrAssign` both replace an existing value and return `true`
only when a new key was inserted. `Upsert` invokes an updater for existing
state, and `Remove` reports whether the key was removed. `Get`, `TryGet`, and
`GetOptional` return value copies. `Contains`, `Size`, `Empty`, `Capacity`, and
`LoadFactor` inspect state.

`Clear` and `Reserve` are writer operations. `ForEach` and
`WeaklyConsistentForEach` provide their named consistency models; callbacks
must not recursively violate the map’s access protocol.

## Reclamation

`ManualQuiesce` requires explicit `Quiesce()` at a real application safe point.
`LocalEpoch` waits for readers pinned before retirement. `HazardPointers` waits
for table/chain hazards to clear. `PendingRetired`, `ReclaimedRetired`, and
`ActiveReaders` are observability counters.

The map is non-copyable and non-movable. Stop all users and satisfy the chosen
reclamation policy before destruction. Its allocator must remain alive through
final table/node reclamation.
