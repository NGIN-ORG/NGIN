---
title: Choosing a hash or checksum
description: Select FNV, CRC, checksums, MACs, or signatures based on stability and threat model.
---

# Choosing a hash or checksum

| Requirement | Facility |
| --- | --- |
| Fast deterministic non-secret hash | `FNV1a32` / `FNV1a64` |
| Detect accidental transmission/storage corruption | configured CRC or checksum |
| Authenticate bytes with a shared secret | Crypto MAC |
| Authenticate bytes with a private/public key pair | Crypto signature |

FNV overloads accept byte pointers, character pointers, or `std::string_view`
and are `constexpr`. The default offset/prime parameters implement standard
FNV-1a widths; custom template parameters create a different algorithm contract.

CRC uses a `CRCModel` describing polynomial, initialization, reflection, and
final XOR. Two implementations only interoperate when every model parameter and
input byte order matches. Checksums have the same need for a named algorithm.

## Persisted values

Before storing or transmitting a hash, specify:

1. algorithm and complete parameters;
2. canonical input encoding and field order;
3. output width and byte order;
4. version/migration behavior.

Do not hash native object memory as a stable format: padding, endianness,
compiler layout, and uninitialized bytes can differ.

## Threat model

Collision resistance is not the same as keyed authentication. FNV, CRC, and
ordinary checksums are forgeable by an attacker who controls the data. Use
[NGIN.Crypto](../cryptography/hash-mac-kdf.md) for that boundary.
