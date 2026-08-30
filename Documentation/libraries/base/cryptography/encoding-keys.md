---
title: Encodings and key material
description: Encode bytes strictly and move parsed SPKI, PKCS#8, PEM, and DER data through explicit boundaries.
---

# Encodings and key material

Encoding changes representation; it does not encrypt, authenticate, or make
data safe. NGIN.Crypto keeps byte encoding, structured key formats, and backend
key operations separate so each boundary is visible.

## Encode and decode bytes

```cpp
#include <NGIN/Crypto/Encoding.hpp>

auto text = NGIN::Crypto::Encoding::EncodeHex(bytes);
if (!text)
    return text.error();

auto decoded = NGIN::Crypto::Encoding::DecodeHex(*text);
```

The same owned/`Into` pattern exists for Base64 and Base64Url. `Encode...`
returns owned output; `Encode...Into` writes into caller storage. Decoders are
strict and return `EncodingError` or `ParseError` rather than silently ignoring
malformed input.

Use standard Base64 for protocols that specify it and Base64Url for URL-safe
opaque values. Do not hand-roll token parsing by splitting Base64Url strings;
use the token APIs, which also enforce signature and policy rules.

## Parse structured formats

`ParsePem` returns bounded blocks containing a label and decoded bytes. The
label is metadata, not proof that the bytes contain a valid or trusted key.
DER helpers enforce definite, minimally encoded lengths and bounded elements.

Above DER, NGIN.Crypto models:

- `SubjectPublicKeyInfo` (SPKI) for public keys;
- `PrivateKeyInfo` (PKCS#8) for private keys;
- encrypted PKCS#8 envelopes for supported PBES2 combinations.

Typed import helpers validate algorithm identity and byte shape before
creating `Ed25519`, `X25519`, or ECDSA P-256 wrappers. Provider operations still
perform cryptographic validation.

## Preserve the boundary

Public material can live in ordinary buffers. Private-key bytes and decrypted
PKCS#8 payloads are secrets. Move them into secret storage early, keep password
spans transient, and avoid string conversions. Parsing a certificate or key is
not equivalent to validating its origin, permitted purpose, or trust.

Next: choose [hash, MAC, and KDF](./hash-mac-kdf.md), or inspect
[encoding and key declarations](../../../reference/cpp/base/crypto/encoding-keys.md).
