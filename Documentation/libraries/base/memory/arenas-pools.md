---
title: Arenas and pools
description: Use LinearAllocator, fixed blocks, segregated pools, and ObjectPool with explicit capacity and reclamation.
---

# Arenas and pools

## Phase allocation with LinearAllocator

```cpp
#include <NGIN/Memory/LinearAllocator.hpp>

NGIN::Memory::LinearAllocator arena {1024 * 1024};
auto checkpoint = arena.Mark();

void* scratch = arena.Allocate(4096, 64);
if (!scratch) {
    return HandleCapacityExceeded();
}

BuildTemporaryData(scratch);
arena.Rollback(checkpoint);
```

The arena owns one upstream slab. Allocation bumps a pointer; individual
`Deallocate` does nothing. `Rollback(marker)` reclaims everything after a valid
marker and `Reset()` reclaims the whole slab. Neither runs destructors for
objects placed in that memory—destroy non-trivial objects before rollback/reset.

The allocator is move-only and not thread-safe. Moving transfers the slab.
Pointers remain addresses into that slab but their lifetime remains tied to the
new owner and its next reset/destruction.

## FixedBlockAllocator

`FixedBlockAllocator<BlockSize, BlockCount, Alignment, Upstream>` obtains one
bounded slab and serves exactly `BlockCount` blocks. Requests larger than the
payload or incompatible with alignment fail. `AvailableBlocks` and
`InvalidDeallocations` expose capacity and misuse diagnostics.

It has precise ownership, but deallocation must point at a real block start.
An interior pointer is not a valid deallocation even though it lies in the
owned slab.

## SegregatedPoolAllocator

This allocator maintains bounded classes for 16, 32, 64, 128, 256, and 512-byte
blocks. The request is routed to the smallest fitting class. Larger or
over-aligned requests fail. Use it for a known small-object distribution, not
as a silent general heap.

## ObjectPool

```cpp
NGIN::Memory::ObjectPool<Particle, 1024> pool;
Particle* particle = pool.Create(position, velocity);
if (!particle) {
    return false;
}

pool.Destroy(particle);
```

`Create` constructs in a fixed block and returns `nullptr` on capacity
exhaustion; constructor exceptions release the block and propagate. Destroy
only live objects owned by that pool.

## Capacity policy

Bounded allocation is useful only when exhaustion has a defined behavior:
drop optional work, reuse an older object, spill through an explicit fallback,
or fail the enclosing operation. Never add an undocumented system-heap fallback
that defeats the reason capacity was bounded.

