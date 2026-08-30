---
title: NGIN.Base Memory and Containers API
description: Allocator contracts, allocator selection, owning helpers, smart pointers, and allocator-aware containers.
---

# NGIN.Base Memory and Containers API

**Includes:** `<NGIN/Memory.hpp>`, `<NGIN/Containers.hpp>`  
**Target:** `NGIN::Base::Foundation`  
**Namespaces:** `NGIN::Memory`, `NGIN::Containers`

NGIN allocators use explicit byte size and alignment. Containers store an
allocator value—often a non-owning handle—and avoid the standard allocator
protocol.

## Allocator contract

An allocator satisfies:

```cpp
void* Allocate(std::size_t bytes, std::size_t alignment) noexcept;
void Deallocate(void* pointer,
                std::size_t bytes,
                std::size_t alignment) noexcept;
```

Exhaustion or an unrepresentable request returns `nullptr`. Pass the original
size and alignment back to `Deallocate`; instrumentation and some routing
strategies depend on them.

`AllocatorTraits<A>` adds optional `MaxSize`, `Remaining`, `AllocateEx`, and
`OwnershipOf`. Ownership is tri-state: `Owns`, `DoesNotOwn`, or `Unknown`.
Never route a deallocation by treating `Unknown` as `Owns`.

## Choose an allocator

| Need | Type | Important rule |
| --- | --- | --- |
| General heap | `SystemAllocator` | Stateless platform-aligned allocation |
| Temporary arena | `LinearAllocator<Upstream>` | Individual deallocation is a no-op; reset or roll back in bulk |
| Fixed-size objects | `FixedBlockAllocator<...>` | Bounded capacity; rejects larger or over-aligned requests |
| Mixed small blocks | `SegregatedPoolAllocator<...>` | Bounded size classes up to its documented maximum |
| Typed fixed pool | `ObjectPool<T, Capacity>` | Pool must outlive every object |
| Diagnostics | `DebugAllocator<Inner>` | Adds canaries, fill patterns, and bookkeeping overhead |
| Counters | `TrackingAllocator<Inner>` | Accurate deallocation sizes are required |
| Shared state | `ThreadSafeAllocator<Inner, Lock>` | Serializes all inner access |
| Primary then fallback | `TaggedFallbackAllocator<P, S>` | Header tags give deterministic return routing |
| Precise-owner fallback | `FallbackAllocator<P, S>` | Both allocators must report precise ownership |
| Borrow a concrete allocator | `AllocatorRef<A>` | Non-owning; allocator must outlive allocations |
| Borrow any allocator | `PolyAllocatorRef` | Non-owning type erasure; reserve for dynamic boundaries |

## Linear allocator

```cpp
NGIN::Memory::LinearAllocator<> scratch {1024 * 1024};

void* first = scratch.Allocate(1024, 16);
auto marker = scratch.Mark();
void* temporary = scratch.Allocate(4096, 64);

scratch.Rollback(marker); // temporary is invalid now
scratch.Reset();          // every arena allocation is invalid now
```

Markers belong to their allocator state. Reset or rollback invalidates
allocations reclaimed by that operation, including values held by containers.

## Allocation helpers

```cpp
Widget* widget = NGIN::Memory::AllocateObject<Widget>(allocator, argument);
NGIN::Memory::DeallocateObject(allocator, widget);

Widget* items = NGIN::Memory::AllocateArray<Widget>(allocator, count);
NGIN::Memory::DeallocateArray(allocator, items);
```

Object and array helpers pair allocation with construction and destroy objects
before release. Array allocation stores the count metadata needed for
`DeallocateArray`. Owning factories translate exhaustion to `std::bad_alloc`
where their API is exception-based.

`Reallocate` provides a byte-oriented strong guarantee: it allocates and copies
first, then releases the original. If allocation fails, the original remains
valid. A new size of zero releases the old allocation and returns `nullptr`.

## Smart pointers

Use the focused smart-pointer factories so allocation and destruction use the
same allocator. The allocator—or the state referenced by its value—must outlive
the final owner. Do not construct an allocator-backed owner from a temporary
`AllocatorRef` target.

## Containers

| Type | Use |
| --- | --- |
| `Vector<T, Allocator>` | Contiguous growable sequence |
| `Array<T, N>` | Fixed-size inline array |
| `BasicString<Char, Allocator>` / `String` | Owned text storage |
| `FlatHashMap<K, V, ...>` | Cache-friendly open-addressed map |
| `FlatHashSet<T, ...>` | Cache-friendly open-addressed set |
| `ConcurrentHashMap<K, V, ...>` | Explicit concurrent map operations |

Prefer `Reserve` when you know a count. Growth can invalidate pointers,
references, spans, and iterators into contiguous storage. A hash-table rehash
invalidates table-position-dependent access.

Allocator copy and move behavior follows
`AllocatorPropagationTraits<A>`. Move propagation is enabled by default;
copy-assignment does not silently replace the destination allocator unless the
allocator opts in.

## Example: arena-backed vector

```cpp
#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>

NGIN::Memory::LinearAllocator<> arena {64 * 1024};
NGIN::Memory::AllocatorRef allocator {arena};
NGIN::Containers::Vector<int, decltype(allocator)> values {allocator};

values.Reserve(1000);
values.PushBack(42);
```

Destroy `values` before resetting or destroying `arena`. The vector's
allocator handle does not own the arena.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| `Allocate` returns `nullptr` | Capacity, size, or alignment cannot be served | Handle exhaustion or choose a suitable allocator |
| Invalid free/corruption count rises | Wrong pointer, size, alignment, or allocator instance | Preserve allocation metadata and allocator identity |
| Container crashes during destruction | Backing allocator died or reset first | Reverse the lifetime order |
| Race in a stateful allocator | It is shared without synchronization | Give each thread an allocator or wrap it explicitly |

**Source:** [`NGIN/Memory`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Memory), [`NGIN/Containers`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Containers)

