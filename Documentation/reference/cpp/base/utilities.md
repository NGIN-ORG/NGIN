---
title: Utilities API
description: API reference for Any, Callable, StringInterner, SymbolTable, flags, and tag types.
---

# Utilities API

## `Any`

**Header:** `<NGIN/Utilities/Any.hpp>`

```cpp
template<std::size_t SboSize = 32,
         class Allocator = Memory::SystemAllocator,
         class TypeIdPolicy = detail::AnyDefaultTypeIdPolicy>
class Any;
```

Central operations are value construction/`Emplace`, reset, `HasValue`,
`IsInline`, `GetTypeId`, `Size`, `Alignment`, `Is<T>`, `TryCast<T>`, checked
cast, visitation helpers, `MakeView`, allocator access, and raw data access.
`AnyView` and `ConstAnyView` borrow value and descriptor.

## `Callable<R(Args...)>`

**Header:** `<NGIN/Utilities/Callable.hpp>`

An owning copyable type-erased callable with an inline buffer of four pointers.
Construction selects inline or heap state. Invocation uses the declared
signature. Copy/move/empty behavior follows the stored callable and wrapper
operations.

## Interning and symbols

`StringInterner<Allocator, ThreadPolicy>` exposes `InsertOrGet`, `TryGetId`,
`Intern`, `View`, size/byte/statistics queries, and allocator access. The
default `NullMutex` policy is not thread-safe. `SymbolTable` exposes `Intern`,
`TryGet`, `View`, and `Size` using `Meta::SymbolId`.

`LSBFlag`, `MSBFlag`, and public tags provide small bit/dispatch helpers.

[Browse Utilities headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Utilities).
