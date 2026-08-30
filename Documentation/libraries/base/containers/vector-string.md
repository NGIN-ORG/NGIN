---
title: Vectors and strings
description: Use NGIN allocator-aware contiguous containers with explicit capacity, movement, bounds, and code-unit semantics.
---

# Vectors and strings

## Vector operations

`Vector<T, Alloc>` owns contiguous `T` elements and stores `Alloc` by value.
Central operations are `PushBack`, `EmplaceBack`, `PushAt`, `EmplaceAt`,
`PopBack`, `Erase`, `Clear`, `Reserve`, `ShrinkToFit`, `At`, indexing, and
standard-style data/iterator access.

```cpp
NGIN::Containers::Vector<Job> jobs;
jobs.Reserve(128);
Job& created = jobs.EmplaceBack(arguments);
```

`At` performs bounds checking according to the implementation contract;
`operator[]` requires a valid index. `PopBack` requires a non-empty vector.
Growth can throw `std::bad_alloc` when the allocator returns `nullptr`, and
element construction/movement can propagate its own exceptions.

The implementation can steal storage on move only when allocator propagation
or allocator equality/compatibility permits it. Otherwise it relocates
elements. Do not assume moving is always pointer-preserving for a stateful
allocator.

## BasicString

`BasicString<CharT, SBOBytes, Alloc, Growth, Traits>` stores owned code units,
with inline small-buffer capacity and allocator-backed growth. Common aliases
such as `String` select the standard character/configuration.

The string does not infer Unicode character boundaries. `Size()` is a code-unit
count. Use `NGIN::Text::Unicode` for validation, decoding, code-point iteration,
and conversion.

Small-buffer movement can change addresses even without heap allocation.
Treat views and `CStr()`/data pointers as invalidated by non-const mutation or
movement unless the exact operation documents otherwise.

## Allocator lifetime

If `Alloc` is `AllocatorRef`/`PolyAllocatorRef`, its concrete target must
outlive the container and allocated capacity. `Clear` does not necessarily
release capacity. Destroy the container or use a capacity-releasing operation
before destroying/resetting its allocator.

