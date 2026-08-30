---
title: Crypto authenticated encryption
description: API reference for AEAD algorithms, input/result structures, and typed cipher helpers.
---

# Crypto authenticated encryption

**Headers:** `<NGIN/Crypto/Symmetric/Aead.hpp>`, `<NGIN/Crypto/Symmetric.hpp>`

## Core types

`AeadAlgorithm` identifies AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305, and
XChaCha20-Poly1305 families. `GetAeadSizes`, `AeadKeySize`, `AeadNonceSize`, and
`AeadTagSize` return exact byte requirements.

`AeadSealInput` contains a secret key view, nonce, plaintext, and associated
data. `AeadOpenInput` contains key, nonce, ciphertext, associated data, and tag.
`AeadSealResult` owns ciphertext and a `StandardAeadTag`.

## Operations

```cpp
CryptoExpected<AeadSealResult> Seal(
    const CryptoContext&, AeadAlgorithm, const AeadSealInput&);
CryptoExpected<ByteBuffer> Open(
    const CryptoContext&, AeadAlgorithm, const AeadOpenInput&);
```

`SealInto` and `OpenInto` write exact caller storage. Typed algorithm headers
define fixed key/nonce/tag aliases, random key/nonce helpers, and matching
`Seal...`/`Open...` wrappers.

`Open` returns no successful plaintext on tag failure. Callers must preserve
the key/nonce uniqueness contract and pass identical associated data during
open.

**Defined:** [`Aead.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Symmetric/Aead.hpp)
