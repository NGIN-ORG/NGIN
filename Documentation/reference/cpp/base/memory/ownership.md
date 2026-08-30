---
title: Allocation helpers and smart ownership
description: Code reference for object/array allocation helpers and allocator-aware Scoped, Shared, and Ticket types.
---

# Allocation helpers and smart ownership

## Allocation helpers

**Header:** `<NGIN/Memory/AllocationHelpers.hpp>`

| Function family | Contract |
| --- | --- |
| `Reallocate` | Allocate new storage, copy the requested prefix, deallocate old storage on success |
| `AllocateObject<T>` | Allocate and construct one object; deallocate if construction throws |
| object deallocation helper | Destroy one object and return matching storage |
| `AllocateArrayUninitialized<T>` | Allocate tracked array storage without constructing elements |
| `AllocateArray<T>` | Allocate and construct elements with rollback on partial failure |
| array deallocation helper | Recover tracked count, destroy constructed elements, release storage |

Use the exact matching deallocation helper. Array storage contains metadata;
do not pass an interior pointer or free it directly through the allocator.

## Smart ownership

**Header:** `<NGIN/Memory/SmartPointers.hpp>`

### `Scoped<T, Alloc>`

Move-only single ownership. `Get`, dereference, arrow, Boolean conversion,
`Release`, and `Allocator` are the central members. Destruction runs `T` and
deallocates through the stored allocator. `MakeScoped` constructs safely.

### `Shared<T, Alloc>`

Copyable strong ownership through an allocator-backed control block. `Get`,
dereference, `UseCount`, and `Expired` inspect state. `MakeShared`,
`MakeSharedAs`, and `MakeSharedAlias` build ordinary, base/derived, and aliasing
ownership.

### `Ticket<T, Alloc>`

Weak-like control-block observer. `Expired` tests strong ownership and `Lock`
tries to produce a `Shared`. `MakeTicket(shared)` creates one.

The allocator/resource needed by destruction and control-block release must
remain usable for the entire corresponding ownership lifetime.

