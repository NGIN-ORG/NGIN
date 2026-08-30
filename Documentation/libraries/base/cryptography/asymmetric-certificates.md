---
title: Signatures, agreement, and certificates
description: Use asymmetric operations while keeping identity, trust, and key-derivation policy explicit.
---

# Signatures, agreement, and certificates

Asymmetric primitives establish different facts. A signature proves possession
of a private key for specific bytes. Key agreement creates shared secret
material. Certificate parsing exposes identity claims. None of these alone
establishes that a remote peer should be trusted.

## Sign and verify

```cpp
auto pair = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context);
if (!pair)
    return pair.error();

auto signature = NGIN::Crypto::Asymmetric::SignEd25519(
    context, pair->privateKey, message);
if (!signature)
    return signature.error();

auto verified = NGIN::Crypto::Asymmetric::VerifyEd25519(
    context, pair->publicKey, message, *signature);
```

Verification returns `CryptoExpected<void>` so malformed input, unsupported
algorithms, backend failures, and invalid signatures remain distinguishable.
Define the complete signed message format—including context and version—to
prevent a valid signature being replayed in another protocol.

## Derive a shared secret

`GenerateX25519KeyPair` and `DeriveX25519SharedSecret` produce typed keys and a
`FixedSecret<32>`. Feed the shared secret into a KDF with protocol context;
never use raw agreement output directly as an encryption key. Authenticate
the exchanged public keys or the connection remains vulnerable to an active
intermediary.

## Parse certificates without inventing trust

`ParseX509Certificate` exposes certificate structure, names, validity text,
SPKI, signature data, and selected extensions. `CertificateStore` loads custom
collections or platform roots and supports identifier lookups. Parsing and
store membership do not perform path construction, hostname validation,
purpose checks, time checks, constraints enforcement, or revocation policy.

Use `TlsCredentialMaterial` to hand a certificate chain and PKCS#8 private key
to the networking TLS layer. Keep TLS session and peer-validation policy in
NGIN.Networking, where connection identity is known.

For declarations, see the [asymmetric and certificate API](../../../reference/cpp/base/crypto/asymmetric-certificates.md).
