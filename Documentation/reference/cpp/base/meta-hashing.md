---
title: Meta and hashing API
description: API reference for type names/IDs, symbol/reflection identities, traits, FNV, CRC, and checksums.
---

# Meta and hashing API

## Metadata

**Header:** `<NGIN/Meta.hpp>`

| Symbol | Contract |
| --- | --- |
| `TypeName<T>` | Compile-time `rawName`, `qualifiedName`, `unqualifiedName`, `namespaceName` views |
| `TypeId<T>::GetId()` / `GetTypeId<T>()` | `UInt64` FNV-based ID from qualified compiler-derived name |
| `SymbolId` | `UInt32` wrapper; zero is invalid; `IsValid`, `Invalid`, comparison |
| `ModuleIdentity` | Composed module identity with `Create` and `IsValid` |
| `TypeIdentity` | Composed module/type identity with `Create` and `IsValid` |
| `FunctionTraits`, `TypeTraits`, `EnumTraits` | Compile-time shape/metadata facilities |

Compiler-derived type names/IDs are not promised external schema identities.

## Hashing

**Header:** `<NGIN/Hashing.hpp>`

```cpp
constexpr UInt32 FNV1a32(const UInt8*|const char*, UIntSize) noexcept;
constexpr UInt32 FNV1a32(std::string_view) noexcept;
constexpr UInt64 FNV1a64(const UInt8*|const char*, UIntSize) noexcept;
constexpr UInt64 FNV1a64(std::string_view) noexcept;
```

`CRCModel<T, Poly, Init, RefIn, RefOut, XorOut>` specifies a CRC completely.
CRC helpers provide bitwise/table update paths and named model aliases where
declared. `Checksum.hpp` provides non-cryptographic checksum operations.

[Browse Meta](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Meta) and [Hashing](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Hashing) headers.
