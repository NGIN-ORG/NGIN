---
title: Learn metadata and hashing
description: Use compile-time type metadata, stable symbol identity, and non-cryptographic hashes without confusing their contracts.
---

# Learn metadata and hashing

NGIN.Meta describes compile-time shape and NGIN-facing identity. NGIN.Hashing
turns bytes into compact non-secret values. Both are foundational mechanisms;
neither supplies runtime reflection or cryptographic integrity.

## Start here

1. Learn [type names, IDs, symbols, and reflection identity](./meta/identity.md).
2. Use [traits and compile-time inspection](./meta/traits.md) at generic API boundaries.
3. Choose [FNV, CRC, or a cryptographic primitive](./hashing/choosing-a-hash.md).
4. Look up exact symbols in the [Meta and Hashing API](../../reference/cpp/base/meta-hashing.md).

## Identity boundaries

`TypeName<T>` derives compiler-specific diagnostic text. `TypeId<T>` hashes
that qualified name for in-process type discrimination. Those are convenient
for diagnostics and type-erased facilities, but compiler/build changes can
change them.

`SymbolId` is a compact ID produced by a `SymbolTable`. Its numeric value has
meaning only with the table/lifetime that assigned it unless a higher-level
format defines a stable mapping. `ModuleIdentity` and `TypeIdentity` combine
explicit pieces for NGIN reflection registration.

## Hash boundaries

FNV is a deterministic, fast non-cryptographic hash. CRC and checksums detect
accidental corruption. An attacker able to change the payload can also forge
all of them. Use a [MAC or signature](./cryptography/hash-mac-kdf.md) for
adversarial integrity.

If a hash crosses a storage, protocol, cache, or version boundary, specify the
algorithm, canonical input bytes, byte order/encoding, and version. Never rely
on an implementation-defined standard-library hash as persisted identity.
