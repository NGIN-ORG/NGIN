---
title: Hashes, MACs, and key derivation
description: Choose hashing, authentication, key derivation, and password hashing by security purpose.
---

# Hashes, MACs, and key derivation

Hashes, MACs, and KDFs all transform bytes, but they provide different
security properties. Choosing by output length or familiar algorithm name is a
common source of vulnerabilities.

| Need | Use |
| --- | --- |
| Stable digest or non-adversarial integrity | Hash |
| Authenticate bytes with a shared key | MAC |
| Expand or separate existing key material | HKDF |
| Derive a key from a password | Argon2id or policy-approved PBKDF2 |

## Hash data

```cpp
auto digest = NGIN::Crypto::Hashing::Sha256(context, message);
if (!digest)
    return digest.error();
```

`Hash` selects a `HashAlgorithm`; `HashInto` writes into exact caller storage.
A plain hash does not authenticate attacker-controlled content because an
attacker can replace both message and digest.

## Authenticate bytes

`ComputeMac` and `VerifyMac` select a `MacAlgorithm` and receive the key through
a `SecretView`. Prefer `VerifyMac` over comparing tags yourself; it preserves
the implementation's constant-time comparison boundary. A MAC is symmetric:
every verifier can also create valid tags.

## Derive keys

`KeyDerivationParameters` holds one of `HkdfParameters`, `Pbkdf2Parameters`, or
`Argon2idParameters`. Use `DeriveFixedSecret<N>` when the output is a fixed
secret key and `DeriveKeyInto` when storage is owned by the caller.

HKDF is for high-entropy input key material and protocol separation. It is not
a password hash. Password APIs add an encoded, self-describing hash string and
policy checks through `HashPassword`, `VerifyPassword`, and
`PasswordHashNeedsRehash`.

Store salts beside the password hash; they are not secrets. Choose cost and
memory parameters as deployment policy, measure them on production-class
hardware, and increase them over time. Never invent a repeated-hash password
scheme.

Next: [authenticated encryption](./authenticated-encryption.md), or inspect the
[hash, MAC, and KDF API](../../../reference/cpp/base/crypto/hash-mac-kdf.md).
