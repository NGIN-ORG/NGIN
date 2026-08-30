---
title: Type-erased values with Any
description: Store values in Any, inspect type identity, cast safely, and preserve view lifetime.
---

# Type-erased values with `Any`

`Any<SboSize, Allocator, TypeIdPolicy>` owns one value of a runtime-selected
type. Small compatible values fit inline; larger values use the configured
allocator.

```cpp
NGIN::Utilities::Any<> value;
value.Emplace<MySettings>(/* constructor arguments */);

if (auto* settings = value.TryCast<MySettings>()) {
    Use(*settings);
}
```

Use `HasValue`, `IsInline`, `GetTypeId`, `Size`, and `Alignment` to inspect the
erased state. `Is<T>` tests its type. `TryCast<T>` returns null on mismatch;
the throwing/checked cast variant should only be used when mismatch violates a
known invariant.

The default type-ID policy derives identity from `Meta::TypeName`. It is
appropriate within its documented build/process domain, not as a persistent
schema identity. Supply a deliberate policy when a different identity contract
is required.

`MakeView()` creates `AnyView`; the const overload creates `ConstAnyView`.
Views borrow both stored data and its descriptor. Resetting, moving,
re-emplacing, or destroying the `Any` invalidates dependent views/pointers.

Copying `Any` copies the contained value and can allocate or throw. Moving an
inline value may invoke its move constructor; moving heap-backed state can
transfer storage. Consult the stored type's own exception/lifetime behavior.
