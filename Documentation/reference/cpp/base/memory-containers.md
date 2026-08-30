---
title: NGIN.Memory and Containers API
description: Symbol-level entry point for allocators, allocation helpers, smart pointers, vectors, strings, and hash maps.
---

# NGIN.Memory and Containers API

**Umbrellas:** `<NGIN/Memory.hpp>`, `<NGIN/Containers.hpp>`  
**Namespaces:** `NGIN::Memory`, `NGIN::Containers`  
**Target:** `NGIN::Base::Foundation`

Start with [Learn Memory](../../../libraries/base/memory.md) or
[Learn Containers](../../../libraries/base/containers.md) if you need usage
and decision guidance.

## Memory

| Surface | Symbols | Reference |
| --- | --- | --- |
| Allocator contract | `AllocatorConcept`, `AllocatorTraits`, `AllocatorRef`, `PolyAllocatorRef`, `MemoryBlock`, `Ownership` | [Allocator references](./memory/allocator-references.md) |
| Concrete allocators | `SystemAllocator`, `LinearAllocator`, `FixedBlockAllocator`, `SegregatedPoolAllocator`, `ObjectPool` | [Allocators](./memory/allocators.md) |
| Composition | `FallbackAllocator`, `TaggedFallbackAllocator`, `TrackingAllocator`, `DebugAllocator`, `ThreadSafeAllocator` | [Allocator composition](./memory/composition.md) |
| Construction | `AllocateObject`, array helpers, `Reallocate` | [Allocation and ownership](./memory/ownership.md) |
| Smart ownership | `Scoped`, `Shared`, `Ticket`, `Make*` functions | [Allocation and ownership](./memory/ownership.md#smart-ownership) |

## Containers

| Surface | Symbols | Reference |
| --- | --- | --- |
| Contiguous storage | `Vector<T, Alloc>` | [Vector](./containers/vector.md) |
| Owned strings | `BasicString`, `String` aliases | [String](./containers/string.md) |
| Flat hash table | `FlatHashMap<K,V,...>` | [FlatHashMap](./containers/flat-hash-map.md) |
| Concurrent hash table | `ConcurrentHashMap<K,V,...>`, `ReclamationPolicy` | [ConcurrentHashMap](./containers/concurrent-hash-map.md) |

## Contract conventions

- Allocator `Allocate` returns `nullptr` for unsupported/exhausted requests.
- Allocator references borrow and must not outlive the concrete allocator.
- Containers store allocator values/handles and may throw `std::bad_alloc`
  when growth cannot allocate.
- Growth, shifting, rehash, erase, movement, reset, and destruction define
  address lifetime; no general stable-reference promise exists.
- Allocator synchronization does not make a container or object thread-safe.

