---
title: Concurrent hash maps
description: Use ConcurrentHashMap operations and choose manual, epoch, or hazard-pointer reclamation deliberately.
---

# Concurrent hash maps

`ConcurrentHashMap<Key, Value, Hash, Equal, Alloc, ShardCount, Policy>` divides
keys among shards. Readers pin reclamation state while writers publish cloned
immutable chains/tables and retire the replaced storage.

## Safe public operations

```cpp
Map sessions {1024, allocator};
sessions.InsertOrAssign(id, session);

Session copy;
if (sessions.TryGet(id, copy)) {
    Use(copy);
}

sessions.Upsert(id, initial, [](Session& existing) {
    existing.Refresh();
});
```

`Get`, `TryGet`, and `GetOptional` return/copy values rather than expose an
internal reference. `Insert`, `InsertOrAssign`, `Upsert`, `Remove`, `Clear`, and
`Reserve` perform writer operations. Value/key copy/move and allocator failure
can propagate according to their operation contracts.

## Reclamation policy

| Policy | Reader publication | Reclamation responsibility |
| --- | --- | --- |
| `ManualQuiesce` | Read section tracked without automatic writer reclamation | Owner calls `Quiesce` only at a true safe point |
| `LocalEpoch` | Reader pins shard epoch | Retired objects wait for earlier readers |
| `HazardPointers` | Reader publishes current table and chain head hazards | Retired objects wait until hazards clear |

A stalled reader can delay reclamation under epoch/hazard policies. Observe
`PendingRetired`, `ReclaimedRetired`, and `ActiveReaders` as diagnostics and
capacity signals, not synchronization barriers.

## Iteration

`ForEach` and `WeaklyConsistentForEach` have distinct consistency and blocking
costs. A weak traversal may observe a mix of states during concurrent writes.
Never mutate the same map from a callback unless the exact method explicitly
allows it; doing so can deadlock or recursively enter writer protocols.

## Shutdown

Stop readers/writers, join or drain their execution, perform required final
quiescence/reclamation, then destroy the map and allocator. A thread-safe
allocator cannot substitute for the map’s reclamation protocol.

