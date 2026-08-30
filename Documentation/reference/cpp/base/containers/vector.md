---
title: Vector
description: Code reference for NGIN.Containers Vector construction, modifiers, capacity, access, movement, and invalidation.
---

# `Vector<T, Alloc>`

**Header:** `<NGIN/Containers/Vector.hpp>`  
**Namespace:** `NGIN::Containers`  
**Defined:** [`Vector.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Containers/Vector.hpp#L31)

## Template and construction

```cpp
template<class T, Memory::AllocatorConcept Alloc = Memory::SystemAllocator>
class Vector;

Vector();
explicit Vector(std::size_t initialCapacity, Alloc alloc = {});
Vector(std::initializer_list<T> init, Alloc alloc = {});
```

Copying copies elements and allocator according to its copy behavior. Move
construction transfers allocator and storage. Move assignment steals storage
only when propagation/equality compatibility permits; otherwise it moves
elements into destination-owned storage.

## Modifiers and capacity

```cpp
T& PushBack(const T&);
T& PushBack(T&&);
template<class... Args> T& EmplaceBack(Args&&...);
void PushAt(UIntSize index, T value);
template<class... Args> void EmplaceAt(UIntSize index, Args&&...);
void PopBack();
void Erase(UIntSize index);
void Clear() noexcept;
void Reserve(UIntSize capacity);
void ShrinkToFit();
UIntSize Size() const noexcept;
UIntSize Capacity() const noexcept;
```

Growth translates allocator exhaustion to `std::bad_alloc`. Element
construction/movement exceptions follow the operation’s documented guarantee.

## Access

`At` checks bounds and throws `std::out_of_range`; `operator[]` requires a valid
index. `data`, `begin`, and `end` expose contiguous pointers. `GetAllocator`
returns the stored allocator.

## Invalidation

Capacity change invalidates every pointer/reference/iterator. Insert/erase can
invalidate at and after the shifted position. Move assignment may either steal
or relocate; do not assume address preservation without proving allocator
compatibility.

