---
title: Crypto hash, MAC, and KDF
description: API reference for digests, keyed authentication, derivation parameters, and password hashing.
---

# Crypto hash, MAC, and KDF

All provider-backed operations take `const Backend::CryptoContext&` first.

## Hashing

```cpp
UIntSize DigestSize(HashAlgorithm) noexcept;
CryptoExpected<void> HashInto(const CryptoContext&, HashAlgorithm,
                              ConstByteSpan input, ByteSpan output) noexcept;
CryptoExpected<ByteBuffer> Hash(const CryptoContext&, HashAlgorithm,
                                ConstByteSpan input);
```

Typed `Sha256`/`Sha256Into` and `Sha512`/`Sha512Into` return fixed digest types.

## Message authentication

`MacTagSize` reports output size. `ComputeMac`/`MacInto` produce a tag from a
`SecretView`; `VerifyMac` authenticates a supplied tag. Typed HMAC-SHA-256 and
HMAC-SHA-512 wrappers use fixed tag types.

## Key derivation

`KeyDerivationParameters` holds exactly one parameter object for `Hkdf`,
`Pbkdf2`, or `Argon2id` and reports its `KdfAlgorithm`. `DeriveKey` returns an
ordinary owned buffer, `DeriveKeyInto` writes caller storage, and
`DeriveFixedSecret<N>` returns secret storage.

## Password hashing

```cpp
CryptoExpected<PasswordHashString> HashPassword(
    const CryptoContext&, SecretView password, const PasswordHashOptions& = {});
CryptoExpected<void> VerifyPassword(
    const CryptoContext&, SecretView password, std::string_view encodedHash);
CryptoExpected<bool> PasswordHashNeedsRehash(
    const CryptoContext&, std::string_view encodedHash,
    const PasswordHashOptions& = {});
```

`PasswordHashString` owns the encoded verifier through `Value()`/`String()`;
it is not the password itself.

[Browse Hashing](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Hashing), [MAC](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Mac), and [KDF](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Kdf) headers.
