---
title: Crypto asymmetric and certificate API
description: API reference for typed keys, signatures, agreement, RSA interoperability, X.509, and stores.
---

# Crypto asymmetric and certificate API

## Typed keys and operations

`PublicKey<Tag, Size>` and `PrivateKey<Tag, Size>` prevent keys for different
algorithms from mixing. `KeyPair<Public, Private>` owns the corresponding pair.

| Family | Central operations |
| --- | --- |
| Ed25519 | `GenerateEd25519KeyPair`, `SignEd25519`, `VerifyEd25519` |
| X25519 | `GenerateX25519KeyPair`, `DeriveX25519SharedSecret` |
| ECDSA P-256 | typed keys and provider-backed sign/verify bridges |
| RSA | PSS/SHA-256 sign/verify and OAEP/SHA-256 encrypt/decrypt over DER key formats |

Generic signature operations are `Sign`, `SignInto`, and `Verify`, selected by
`SignatureAlgorithm`. X25519 returns `X25519SharedSecret`, an alias for
`Memory::FixedSecret<32>`.

## Certificates

```cpp
CryptoExpected<Certificate> ParseX509Certificate(ConstByteSpan der);
CryptoExpected<void> VerifyCertificateSignature(
    const CryptoContext&, const Certificate&, const SubjectPublicKeyInfo&);
```

`Certificate` owns parsed serial, issuer/subject, validity, SPKI, signature,
and selected extension fields. `CertificateChain` groups certificates.

`CertificateStore` exposes `Info`, indexed access, size/empty checks, and
lookups by subject DER, subject key identifier, or authority key identifier.
`CreateCustomCertificateStore` creates an in-memory store;
`OpenPlatformRootCertificateStore` loads platform roots; the diagnostics
variant reports per-source failures.

`TlsCredentialMaterial` is a data handoff containing a certificate chain and
private-key information. Path building, hostname checks, purpose/time policy,
constraints, and revocation are outside these parsing/store declarations.

[Browse Asymmetric](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Asymmetric), [Signatures](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Signatures), and [Certificates](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Certificates) headers.
