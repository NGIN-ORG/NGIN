---
title: Crypto random and secret storage
description: API reference for platform entropy, fixed random values, Secret, and SecureBuffer.
---

# Crypto random and secret storage

## Random operations

**Headers:** `<NGIN/Crypto/Random/RandomBytes.hpp>`, `<NGIN/Crypto/Random/SecureRandom.hpp>`

```cpp
bool Random::IsAvailable() noexcept;
CryptoExpected<void> Random::Fill(ByteSpan output) noexcept;
CryptoExpected<ByteBuffer> Random::RandomBytes(UIntSize size);

template<UIntSize Size>
CryptoExpected<FixedBytes<Size>> Random::RandomBytes() noexcept;
```

`Fill` and both `RandomBytes` overloads use the platform secure random source,
not a selected `CryptoContext`. `EntropySource` is the separate non-owning
adapter used by backend/test sources.

## Secret storage

**Headers:** `<NGIN/Crypto/Memory/Secret.hpp>`, `<NGIN/Crypto/Memory/SecureBuffer.hpp>`

```cpp
template<class T> class Memory::Secret;
template<UIntSize Size> using Memory::FixedSecret = Secret<FixedBytes<Size>>;
using Memory::DynamicSecret = SecureBuffer;
```

`Secret<T>` is move-only, wipes its value on move/destruction, and exposes
`FromValue`, `Generate`, `View`, `Bytes`, `UnsafeMutableView`, and
`UnsafeMutableBytes`. `Generate()` is available for supported fixed-byte
values and fills them through `Random::Fill`.

`SecretView` borrows secret bytes; it does not extend the owner's lifetime.
`ZeroMemory` and constant-time equality helpers provide lower-level operations
for implementations that cannot keep values entirely inside secret types.

**Defined:** [`Secret.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Memory/Secret.hpp)
