---
title: Allocator concepts and references
description: Code reference for AllocatorConcept, AllocatorTraits, AllocatorRef, PolyAllocatorRef, MemoryBlock, and Ownership.
---

# Allocator concepts and references

**Headers:** `<NGIN/Memory/AllocatorConcept.hpp>`, `<NGIN/Memory/AllocatorRef.hpp>`, `<NGIN/Memory/PolyAllocatorRef.hpp>`  
**Namespace:** `NGIN::Memory`

## Minimal allocator contract

```cpp
void* Allocate(std::size_t bytes, std::size_t alignment) noexcept;
void Deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept;
```

`AllocatorConcept<A>` validates this surface. Zero/invalid/unrepresentable or
exhausted allocation requests return `nullptr`. Callers should deallocate with
matching size and alignment.

`AllocatorTraits<A>` probes optional `MaxSize`, `Remaining`, `AllocateEx`, and
`OwnershipOf` capabilities without making them mandatory hot-path virtual
operations.

## `Ownership` and `MemoryBlock`

`Ownership` is tri-state: `Owns`, `DoesNotOwn`, or `Unknown`. Routing code must
not interpret `Unknown` as ownership. `MemoryBlock` carries pointer, granted
size, alignment, and an allocator-defined cookie from `AllocateEx`.

## `AllocatorRef<A>`

```cpp
template<class A>
class AllocatorRef {
public:
    explicit AllocatorRef(A& allocator) noexcept;
    void* Allocate(std::size_t, std::size_t) noexcept;
    void Deallocate(void*, std::size_t, std::size_t) noexcept;
    std::size_t MaxSize() const noexcept;
    std::size_t Remaining() const noexcept;
    Ownership OwnershipOf(const void*) const noexcept;
};
```

This is a typed borrowed pointer. It has no empty state and its target must
outlive every handle and allocation user.

## `PolyAllocatorRef`

The erased form stores an object pointer and vtable. Construct it from a
concrete `AllocatorConcept` lvalue. `HasValue`/Boolean conversion inspect an
installed target. An empty reference returns `nullptr`/empty block for
allocation and reports default/unknown optional capabilities.

Both reference types are non-owning and add no synchronization.

