---
title: Learn NGIN.Utilities
description: Use type erasure, callables, interned strings, symbol tables, errors, flags, and tags with explicit ownership.
---

# Learn NGIN.Utilities

NGIN.Utilities contains small reusable contracts shared across NGIN.Base. Use
them when they make ownership or type behavior clearer; do not use a generic
utility to hide a domain concept that deserves its own type.

## Start here

1. Store heterogeneous values with [`Any` and its views](./utilities/any.md).
2. Own invocable state with [`Callable`](./utilities/callable.md).
3. Deduplicate names with [string interning and symbol tables](./utilities/interning-symbols.md).
4. Look up exact declarations in the [Utilities API](../../reference/cpp/base/utilities.md).

## Selection map

| Need | Facility |
| --- | --- |
| Value or typed failure | `Expected<T, E>` |
| Value or reasonless absence | `Optional<T>` |
| Small generic cross-domain error | `ErrorInfo` |
| Owning type-erased value | `Any` |
| Non-owning view of an `Any` value | `AnyView` / `ConstAnyView` |
| Owning type-erased invocation | `Callable<R(Args...)>` |
| Deduplicated stable string storage | `StringInterner` |
| Name-to-`SymbolId` mapping | `SymbolTable` |
| Bit position manipulation | `LSBFlag`, `MSBFlag` |
| Constructor/dispatch intent | public tag types |

Prefer the standard library when its ownership and behavior already fit. The
NGIN alternatives matter where they integrate with NGIN allocators, type IDs,
symbols, or shared conventions.

## Lifetime rule

Type erasure does not erase lifetime. An `Any` owns its value; an `AnyView`
borrows it. A `Callable` owns its stored callable, but that callable may itself
capture references. An interner owns bytes backing returned string views, so
those views cannot outlive or ignore mutation rules of the interner.
