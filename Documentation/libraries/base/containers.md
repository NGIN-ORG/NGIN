---
title: Learn NGIN.Containers
description: Use allocator-aware vectors, strings, flat hash tables, and concurrent hash maps with explicit growth and invalidation.
---

# Learn NGIN.Containers

NGIN.Containers provides containers that use the NGIN.Memory allocator concept
directly. They make allocation policy a visible part of the C++ type and retain
NGIN naming and failure/invalidation contracts.

Use them when allocator choice, bounded capacity, ownership routing, or NGIN
integration matters. Standard containers remain appropriate when their
allocator and API model already fit the boundary.

## Container map

| Container | Shape | Main reason to choose it |
| --- | --- | --- |
| `Vector<T, Alloc>` | Contiguous growable sequence | Explicit NGIN allocator and predictable reserve/growth |
| `BasicString<Char, ...>` / `String` | Owned code-unit string with small-buffer storage | Allocator-aware text storage |
| `FlatHashMap<K,V,...>` | Open-addressed key/value table | Cache-friendly lookup and iteration |
| `ConcurrentHashMap<K,V,...>` | Sharded copy-on-write chains with reclamation | Explicit concurrent read/write operations |

## Your first allocator-aware vector

```cpp
#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>

#include <iostream>

int main() {
    NGIN::Memory::LinearAllocator arena {64 * 1024};
    NGIN::Memory::AllocatorRef allocator {arena};
    NGIN::Containers::Vector<int, decltype(allocator)> values {0, allocator};

    values.Reserve(3);
    values.PushBack(10);
    values.PushBack(20);
    values.PushBack(30);

    for (int value : values) {
        std::cout << value << '\n';
    }

    values.Clear();
    arena.Reset();
}
```

`Clear` destroys elements but retains vector storage. The arena may be reset
only after every container/object using its allocations has released or
abandoned that storage. Here the vector remains alive after reset, so it must
not be used again; a safer production pattern scopes the vector before reset:

```cpp
{
    VectorUsingArena values {allocator};
    // use values
} // destroys vector
arena.Reset();
```

Read [vectors and strings](./containers/vector-string.md) for construction,
growth, invalidation, bounds, and allocator propagation.

## Growth and invalidation

Any operation that grows/reallocates contiguous or flat storage can invalidate
pointers, references, and iterators. `Reserve` is both a performance tool and a
correctness boundary when code temporarily retains addresses.

| Operation | Typical invalidation |
| --- | --- |
| `Vector::Reserve`/growth | All element addresses and iterators |
| Vector insertion/erase without growth | At or after the shifted position |
| Flat hash insertion causing rehash | All bucket references/iterators |
| Flat hash erase | Erased entry and entries relocated by backward shift |
| `ConcurrentHashMap` update | Returned values are copies; internal nodes are not exposed |

Do not infer standard-container invalidation rules where NGIN’s documented
contract differs. Read [invalidation and allocators](./containers/invalidation-allocators.md).

## Flat hash tables

```cpp
NGIN::Containers::FlatHashMap<int, User> users;
users.Insert(42, User {"Ada"});

if (auto* user = users.GetPtr(42)) {
    Show(*user);
}
```

The table uses open addressing and a maximum load factor before growth. Prefer
`GetPtr`/`Contains` for absence-aware lookup. `Get`/`GetRef` have their own
failure/precondition behavior; use the reference rather than guessing.

Read [flat hash tables](./containers/flat-hash.md) for insertion, lookup,
heterogeneous keys, rehash, and type requirements.

## Concurrent hash map

`ConcurrentHashMap` is not a mutex-wrapped `FlatHashMap`. Readers enter a
reclamation protocol and writers replace immutable chains/tables per shard.
Values are obtained as copies or through controlled callbacks; internal node
references are not exposed.

The reclamation policy is part of the type:

- `ManualQuiesce`: callers invoke `Quiesce` at known safe points;
- `LocalEpoch`: readers pin a shard-local epoch;
- `HazardPointers`: readers publish table and chain hazards.

Read [concurrent hash maps](./containers/concurrent-hash-map.md) before choosing
a policy or using `WeaklyConsistentForEach`.

## Common mistakes

| Mistake | Result | Fix |
| --- | --- | --- |
| Resetting an arena while a container still has capacity from it | Dangling storage | Destroy/release every user before reset |
| Keeping an element pointer across vector growth | Dangling pointer | Reserve first or reacquire after mutation |
| Treating string size as Unicode code points | Incorrect text logic | Size counts code units; use Unicode APIs |
| Assuming a map reference survives insert/erase | Rehash/relocation invalidates it | Keep keys/owned values, not bucket references |
| Using concurrent map internal lifetime assumptions | Reclamation race | Use copies/callback APIs and chosen quiescence contract |
| Sharing an ordinary container because its allocator is thread-safe | Container data races | Synchronize the container or choose a concurrent type |

## Continue

1. [Vectors and strings](./containers/vector-string.md)
2. [Flat hash tables](./containers/flat-hash.md)
3. [Concurrent hash maps](./containers/concurrent-hash-map.md)
4. [Invalidation and allocators](./containers/invalidation-allocators.md)
5. [Containers API reference](../../reference/cpp/base/containers/index.md)
