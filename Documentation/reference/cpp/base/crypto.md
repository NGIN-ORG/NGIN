---
title: NGIN.Crypto API
description: Code-grounded index of providers, secure memory, encodings, hashes, encryption, keys, and certificates.
---

# NGIN.Crypto API

**Umbrella header:** `<NGIN/Crypto.hpp>`  
**Namespace:** `NGIN::Crypto`  
**Target:** `NGIN::Base::Crypto`

NGIN.Crypto owns provider-neutral contracts and safety boundaries. Runtime
algorithm support comes from the selected `Backend::CryptoContext`.

## Symbol groups

| Area | Central declarations | Reference |
| --- | --- | --- |
| Providers and failures | `CryptoContext`, `BackendOptions`, `CryptoExpected<T>`, `CryptoError` | [Providers and errors](./crypto/providers-errors.md) |
| Random and secrets | `Random::Fill`, `RandomBytes`, `Secret<T>`, `SecureBuffer` | [Random and secrets](./crypto/random-secrets.md) |
| Encoding and key formats | Hex/Base64/Base64Url, PEM, DER, SPKI, PKCS#8 | [Encoding and keys](./crypto/encoding-keys.md) |
| Hash, MAC, KDF | `Hash`, `ComputeMac`, `VerifyMac`, `DeriveKey`, password hashing | [Hash, MAC, KDF](./crypto/hash-mac-kdf.md) |
| Authenticated encryption | `AeadAlgorithm`, `Seal`, `Open`, typed algorithm helpers | [AEAD](./crypto/aead.md) |
| Asymmetric and certificates | Ed25519, X25519, ECDSA, RSA, X.509, stores | [Asymmetric and certificates](./crypto/asymmetric-certificates.md) |

Results report recoverable failures. Authentication failure never produces
usable output. Capability checks and provider selection belong at startup;
secret lifetime and nonce allocation belong in the calling protocol.

[Learn cryptography from the beginning](../../../libraries/base/cryptography.md).  
[Browse all Crypto headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto).
