---
title: Type and symbol identity
description: Distinguish compiler-derived type identity, table-local symbols, and composed reflection identity.
---

# Type and symbol identity

`TypeName<T>` exposes `rawName`, `qualifiedName`, `unqualifiedName`, and
`namespaceName` as compile-time string views derived from the compiler's type
signature. It is excellent diagnostic text and useful for internal type
discrimination.

```cpp
constexpr auto name = NGIN::Meta::TypeName<MyType>::qualifiedName;
constexpr auto id = NGIN::Meta::GetTypeId<MyType>();
```

`TypeId<T>::GetId()` hashes the qualified name to `UInt64`. Because its source
is compiler/build-derived, do not persist it as a durable schema or protocol
identifier unless the product explicitly pins and versions that representation.

## Symbols

`SymbolId` wraps a `UInt32`, with zero reserved as invalid. Check `IsValid()`;
construct invalid state with `SymbolId::Invalid()`. A `SymbolTable` maps strings
to these compact values. The table owns the mapping and therefore its numeric
IDs are table-relative.

## Reflection identities

`ModuleIdentity` combines explicit module name/version inputs. `TypeIdentity`
combines a module identity with type namespace/name and version inputs. Their
`Create` functions produce deterministic composed values from those pieces.
They identify registration records; actual fields, methods, attributes, and
runtime lookup live in NGIN.Reflection.

Choose one identity contract and document its stability domain: current
process, current symbol table, one build, or a versioned external format.
