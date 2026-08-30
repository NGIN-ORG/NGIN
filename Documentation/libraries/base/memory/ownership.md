---
title: Allocator ownership and smart pointers
description: Distinguish owned allocator state, borrowed allocator references, and allocator-aware Scoped, Shared, and Ticket ownership.
---

# Allocator ownership and smart pointers

## Allocator values are policies or handles

Containers store their allocator template parameter by value. That value may be
a stateless policy (`SystemAllocator`), an owning resource (`LinearAllocator`),
or a borrowed handle (`AllocatorRef<A>`/`PolyAllocatorRef`). “Stored by value”
does not by itself mean backing storage is owned.

```text
concrete allocator owner
        │ borrowed by
        ▼
 AllocatorRef / PolyAllocatorRef → container or smart pointer → allocations
```

The concrete owner must outlive the handle, container, smart pointer control
block, and every allocation routed through it.

## Typed and erased references

`AllocatorRef<A>` preserves the concrete type and forwards allocation,
deallocation, and ownership queries. `PolyAllocatorRef` stores an object pointer
and vtable, allowing runtime erasure at the cost of indirect calls. Both are
non-owning.

Use the typed form in templates and hot paths. Use erased form when a runtime
boundary genuinely needs “some allocator.” A default `PolyAllocatorRef` is
empty; allocation returns `nullptr` and deallocation is a no-op.

## Unique ownership with Scoped

```cpp
auto value = NGIN::Memory::MakeScoped<Widget>(allocator, argument);
if (!value) {
    // Allocation failed.
}
```

`Scoped<T, Alloc>` owns one object and stores the allocator needed to destroy
and deallocate it. It is move-only. `Release()` transfers the raw pointer and
therefore transfers responsibility for destruction/deallocation to the caller.

## Shared and weak-like ownership

`Shared<T, Alloc>` owns through an allocator-backed control block. Copies
increase the strong count. `Ticket<T, Alloc>` observes the control block without
keeping the object alive; `Lock()` returns a `Shared` if a strong owner remains.

`MakeShared`, `MakeSharedAs`, and `MakeSharedAlias` provide ordinary,
base/derived, and aliasing ownership. The allocator stored in the control block
must remain usable until the last strong and ticket/control-block lifetime is
released according to the implementation contract.

## OwnershipOf is tri-state

`Ownership::Owns`, `DoesNotOwn`, and `Unknown` are intentionally distinct.
Never route deallocation by treating `Unknown` as ownership. Use tagged
routing or an allocator with a precise ownership capability.

