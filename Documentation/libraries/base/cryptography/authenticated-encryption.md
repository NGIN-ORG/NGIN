---
title: Authenticated encryption
description: Encrypt with AEAD while preserving key, nonce, associated-data, and authentication boundaries.
---

# Authenticated encryption

Authenticated encryption with associated data (AEAD) encrypts plaintext and
authenticates the ciphertext plus optional unencrypted metadata. NGIN.Crypto
supports AES-GCM and ChaCha20-Poly1305 families when the selected context
reports them.

## Seal a message

```cpp
auto key = NGIN::Crypto::Symmetric::GenerateAes256GcmKey();
auto nonce = NGIN::Crypto::Symmetric::GenerateAesGcmNonce();
if (!key || !nonce)
    return HandleSetupFailure();

auto sealed = NGIN::Crypto::Symmetric::SealAes256Gcm(
    context, *key, *nonce, plaintext, associatedData);
if (!sealed)
    return sealed.error();
```

`AeadSealResult` contains ciphertext and a separate authentication tag. The
recipient needs the algorithm, nonce, associated data, ciphertext, and tag.
Associated data is authenticated but remains visible; protocol headers and
record identifiers are common uses.

## Open safely

Call the matching `Open...` operation with exactly the same associated data.
An `AuthenticationFailed` result means no plaintext may be used. Do not retry
with blank associated data, a shorter tag, another algorithm, or another key.

The generic `Seal`/`Open` and `SealInto`/`OpenInto` APIs take an
`AeadAlgorithm` and explicit input structures. Typed AES-GCM and ChaCha helpers
make valid key, nonce, and tag sizes easier to preserve.

## Nonce rules are part of the protocol

Nonce reuse under the same key can catastrophically break GCM and other AEAD
modes. Do not merely call a random generator in multiple processes and assume
the system-level uniqueness problem is solved. Define how nonces are allocated,
persisted, transmitted, and rotated with the key.

Generate a new key on rotation; never reset a nonce counter under an existing
key. Keep ciphertext and nonces in ordinary storage, but keep keys in secret
types. Authenticate before parsing decrypted bytes.

Next: [asymmetric keys and certificates](./asymmetric-certificates.md), or
inspect the [AEAD API](../../../reference/cpp/base/crypto/aead.md).
