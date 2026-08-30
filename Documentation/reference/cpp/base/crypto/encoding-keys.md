---
title: Crypto encoding and key formats
description: API reference for strict byte encodings, PEM/DER, SPKI, PKCS#8, and typed key bridges.
---

# Crypto encoding and key formats

## Text encodings

**Header:** `<NGIN/Crypto/Encoding.hpp>`

| Format | Owned operations | Caller-storage operations |
| --- | --- | --- |
| Hex | `EncodeHex`, `DecodeHex` | `EncodeHexInto`, `DecodeHexInto` |
| Base64 | `EncodeBase64`, `DecodeBase64` | `EncodeBase64Into`, `DecodeBase64Into` |
| Base64Url | `EncodeBase64Url`, `DecodeBase64Url` | matching `...Into` functions |

Length helpers report required storage. Base64 exposes `Base64Padding`.
Decoders are strict and return a `CryptoExpected` failure for malformed input.

## PEM and DER

`ParsePem` accepts bounded PEM input and optional allowed labels. DER reader
and writer helpers cover bounded TLV primitives including integer, bit/octet
string, object identifier, sequence, and set. They reject indefinite and
non-minimal encodings.

## Key format structures

**Header:** `<NGIN/Crypto/Keys.hpp>`

`SubjectPublicKeyInfo` and `PrivateKeyInfo` represent parsed SPKI and PKCS#8.
Import/export overloads bridge these structures to typed Ed25519, X25519, and
ECDSA P-256 keys. Encrypted private-key helpers preserve or process supported
PKCS#8 encrypted envelopes under explicit password-policy options.

Key-operation bridges accept an explicit `CryptoContext` and parsed key-format
structure. Format validation does not establish key origin, authorization, or
certificate trust.

[Browse Encoding headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Encoding).  
[Browse Keys headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Keys).
