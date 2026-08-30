---
title: Secure random values and secrets
description: Generate cryptographic randomness and keep secret bytes in purpose-built storage.
---

# Secure random values and secrets

Cryptographic random bytes and secret storage solve different problems.
Randomness creates unpredictable values; secret containers reduce the chance
that sensitive bytes survive through ordinary copies and destruction.

## Generate random bytes

```cpp
#include <NGIN/Crypto/Random.hpp>

auto keyBytes = NGIN::Crypto::Random::RandomBytes<32>();
if (!keyBytes)
    return keyBytes.error();

auto tokenBytes = NGIN::Crypto::Random::RandomBytes(24);
```

Use `Random::Fill(ByteSpan)` when storage already exists. It uses the platform
secure random source and returns `EntropyUnavailable` when that source cannot
serve the request. `Random::IsAvailable()` is useful for startup diagnostics,
but the operation result remains authoritative.

Use secure randomness for keys, opaque tokens, salts, and nonces whose
algorithm contract calls for random values. Do not substitute a normal PRNG.
An `EntropySource` is a non-owning adapter useful for providers and tests; a
deterministic test source must never be represented as cryptographically
secure.

## Store secret material

```cpp
#include <NGIN/Crypto/Memory.hpp>

auto generated = NGIN::Crypto::Memory::FixedSecret<32>::Generate();
if (!generated)
    return generated.error();

auto key = std::move(generated).value();
UseKey(key.Bytes());
```

`FixedSecret<N>` is `Secret<FixedBytes<N>>`. `DynamicSecret` provides dynamic
storage. Read through `Bytes()` or `View()`; mutation is intentionally marked
unsafe through `UnsafeMutableBytes()` and `UnsafeMutableView()`.

Secret storage wipes its owned value during destruction and move where
practical. That is a defense-in-depth boundary, not a complete memory-security
system: it cannot prevent logging, debugger inspection, crash dumps, swapping,
or copies made outside the type.

Use ordinary `ByteBuffer` for public keys, digests, ciphertext, and encoded
output. Keep passwords, private keys, symmetric keys, and derived shared
secrets in secret-bearing types for as long as possible.

Next: [encodings and key material](./encoding-keys.md), or inspect the
[random and secret API](../../../reference/cpp/base/crypto/random-secrets.md).
