---
title: Choosing an allocator
description: Select a NGIN.Memory allocator from lifetime, size distribution, capacity, ownership, and concurrency requirements.
---

# Choosing an allocator

Allocator choice starts with object lifetime, not speed claims. Write down when
memory may be reclaimed, whether capacity must be bounded, which sizes occur,
and which execution contexts access the allocator.

## Decision table

| Requirement | Start with | Reclamation |
| --- | --- | --- |
| General unbounded heap behavior | `SystemAllocator` | Individual deallocation |
| Many temporaries sharing one phase lifetime | `LinearAllocator` | `Rollback` or `Reset` |
| One bounded fixed object/block size | `FixedBlockAllocator` | Individual block return |
| Bounded mixture of small sizes up to 512 bytes | `SegregatedPoolAllocator` | Per-size-class block return |
| Bounded typed objects | `ObjectPool<T, Capacity>` | `Destroy(object)` |
| Primary fast store plus general fallback | `TaggedFallbackAllocator` | Header records exact route |

Add wrappers only for a stated need: `TrackingAllocator` for metrics,
`DebugAllocator` for guards/poisoning/invalid frees, and `ThreadSafeAllocator`
when one stateful allocator truly crosses threads.

## Smallest general allocation

```cpp
#include <NGIN/Memory/SystemAllocator.hpp>

NGIN::Memory::SystemAllocator allocator;
void* bytes = allocator.Allocate(256, 64);
if (!bytes) {
    return HandleOutOfMemory();
}

Use(bytes);
allocator.Deallocate(bytes, 256, 64);
```

Pass the same size and alignment to deallocation. Some allocators do not need
them, but decorators, diagnostics, and future routing can.

## Questions to answer

- **Lifetime:** individual free, phase reset, or allocator destruction?
- **Capacity:** may it grow through an upstream, or must exhaustion be bounded?
- **Sizes:** one fixed size, a small known distribution, or arbitrary?
- **Ownership query:** can the allocator definitively classify pointers?
- **Concurrency:** thread-confined or synchronized?
- **Failure:** can callers handle `nullptr`, or does an owning layer translate
  failure to an exception/result?
- **Movement:** does moving a container transfer allocator state and storage?

## Avoid premature wrappers

A thread-safe wrapper serializes allocator operations but does not make objects
allocated from it thread-safe. A tracking wrapper adds counters but does not
prove absence of leaks unless all allocations return through it. A fallback
needs unambiguous deallocation routing; use the tagged form when ownership
queries are not precise.

