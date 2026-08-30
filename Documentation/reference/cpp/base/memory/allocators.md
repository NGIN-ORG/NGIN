---
title: Concrete allocators
description: Code reference for SystemAllocator, LinearAllocator, fixed blocks, segregated pools, and ObjectPool.
---

# Concrete allocators

## `SystemAllocator`

**Header:** `<NGIN/Memory/SystemAllocator.hpp>`

Stateless aligned system allocation. `HasPreciseOwnership` is false and
`OwnershipOf` cannot identify arbitrary system allocations. `MaxSize` is
informational; allocation returns `nullptr` on invalid size/alignment or
resource failure.

## `LinearAllocator<Upstream>`

**Header:** `<NGIN/Memory/LinearAllocator.hpp>`  
**Defined:** [`LinearAllocator.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Memory/LinearAllocator.hpp#L60)

```cpp
explicit LinearAllocator(
    std::size_t capacity,
    Upstream upstream = {},
    std::size_t baseAlignment = /* at least max_align/64 */);

void* Allocate(std::size_t, std::size_t) noexcept;
void Deallocate(void*, std::size_t, std::size_t) noexcept; // no-op
std::size_t MaxSize() const noexcept;
std::size_t Remaining() const noexcept;
std::size_t Used() const noexcept;
ArenaMarker Mark() const noexcept;
void Rollback(ArenaMarker) noexcept;
void Reset() noexcept;
Ownership OwnershipOf(const void*) const noexcept;
```

It owns one upstream slab, is move-only, and is not thread-safe. Reset/rollback
do not run object destructors.

## `FixedBlockAllocator`

`FixedBlockAllocator<BlockSize, BlockCount, Alignment, Upstream>` owns one
fixed slab. It exposes `Capacity`, `AvailableBlocks`, `InvalidDeallocations`,
precise ownership, and block-start validation. Oversized/over-aligned or
exhausted requests return `nullptr`.

## `SegregatedPoolAllocator`

`SegregatedPoolAllocator<BlocksPerClass, Upstream>` owns bounded 16–512-byte
size classes. `Remaining` sums available class payload; invalid frees are
counted. Requests beyond the largest class/alignment fail.

## `ObjectPool<T, Capacity, Upstream>`

`Create(args...)` returns a constructed `T*` or `nullptr` on exhaustion and
rolls back storage if construction throws. `Destroy` ignores null/foreign
pointers, otherwise runs the destructor and returns the block. `Owns`,
`Available`, and `MaxObjects` inspect capacity.

