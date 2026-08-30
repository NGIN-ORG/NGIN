---
title: NGIN.Base Crypto API
description: Providers, capabilities, secure random values, secret storage, encodings, hashes, MACs, KDFs, AEAD, keys, certificates, and tokens.
---

# NGIN.Base Crypto API

**Include:** `<NGIN/Crypto.hpp>`  
**Target:** `NGIN::Base::Crypto`  
**Namespace:** `NGIN::Crypto`

Crypto defines provider-neutral contracts and safety boundaries. It does not
implement cryptographic primitives itself. An algorithm works only when the
selected explicit `CryptoContext` reports provider support.

## Results and errors

Recoverable operations return `CryptoExpected<T>`. `CryptoErrorCode`
distinguishes invalid arguments, insufficient output storage, unsupported
algorithms, unavailable backends, entropy failures, parse failures,
authentication failures, and policy rejection.

Do not reduce verification errors to a bare `false`; diagnostics are useful
for configuration and incident analysis. Do not expose secret data in those
diagnostics.

## Select and inspect a provider

```cpp
auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
if (!context) return Report(context.error());

if (!context->Supports(NGIN::Crypto::AeadAlgorithm::Aes256Gcm)) {
    return ReportUnsupported();
}
```

Use `CreatePlatformContext()` to request only platform facilities, or
`CreatePackageContext("openssl")` for an explicitly named packaged provider.
Use `CreateContextWithDiagnostics()` when startup needs to explain why
providers were rejected. Use `DescribeSupport(algorithm)` for a readable
capability reason.

Provider selection is policy, not a silent fallback detail. Require the
algorithm set the application actually needs during startup.

## Secure random values

```cpp
auto key = NGIN::Crypto::Random::RandomBytes<32>();
if (!key) return key.error();

std::array<NGIN::Byte, 24> nonce {};
auto filled = NGIN::Crypto::Random::Fill(nonce);
```

Use secure randomness for keys, protocol-specified random nonces, and opaque
tokens. `EntropySource` is non-owning and is also used for deterministic tests;
never label a deterministic source cryptographically secure.

## Secret storage

Use ordinary byte buffers for public keys, digests, ciphertext, and encoded
output. Use secure storage for keys, passwords, private keys, and derived
secrets.

```cpp
NGIN::Crypto::Memory::FixedSecret<32> secret;
auto writable = secret.UnsafeMutableBytes();
```

Secret containers wipe on destruction and move where practical. Avoid making
secret `std::string` copies. A wipe reduces residual-memory exposure; it does
not make swapping, crash dumps, logging, or application misuse safe by itself.

## Encodings and key formats

| API | Use |
| --- | --- |
| `HexEncode` / `HexDecode` | Strict hexadecimal |
| `Base64Encode` / `Base64Decode` | Standard Base64 |
| `Base64UrlEncode` / `Base64UrlDecode` | URL-safe opaque values |
| `ParsePem` | Bounded RFC 7468-style PEM blocks |
| `DerReader` and DER writer helpers | Strict bounded ASN.1 DER primitives |
| SPKI helpers | Public key information |
| PKCS#8 helpers | Private key information and supported encrypted envelopes |

PEM parsing returns label and decoded bytes; it does not establish trust or
validate a key. DER parsing rejects indefinite and non-minimal lengths. Typed
key import helpers validate algorithm/size shape before provider dispatch; the
provider still decides whether key material is cryptographically valid.

## Hash, MAC, KDF, and AEAD

Pass the context explicitly:

```cpp
auto digest = NGIN::Crypto::Hashing::Sha256(*context, message);
```

Available families include SHA-2 hashing, HMAC, HKDF, PBKDF2, AES-GCM, and
ChaCha20-Poly1305 variants when supported. Keep these distinctions clear:

- a hash detects accidental changes only when an attacker cannot replace it;
- a MAC authenticates bytes with a shared secret;
- a password KDF applies cost and salt policy;
- AEAD authenticates ciphertext and associated data;
- nonces must obey the selected algorithm's uniqueness/randomness contract.

Never release AEAD plaintext before authentication succeeds.

## Asymmetric operations

The provider-neutral surface includes Ed25519, X25519, ECDSA P-256, and RSA
interop operations where supported. RSA APIs prefer RSA-PSS/SHA-256 signatures
and RSA-OAEP/SHA-256 encryption; they do not present PKCS#1 v1.5 signing as the
preferred default.

Key-agreement output is secret input to a KDF, not an application encryption
key ready for direct reuse.

## Certificates

X.509 parsing exposes structure, names, validity text, public-key information,
signature data, and selected extensions. Parsing is not certificate path or
hostname validation.

`CertificateStore` supports explicit in-memory collections and platform root
store loading. Store loading and lookup still do not decide whether a peer is
trusted. That decision belongs to a validation policy that checks chain,
purpose, name, time, constraints, and any revocation requirements.

## Tokens

`Tokens::ValidateJwt` requires an explicit allowed-algorithm policy, rejects
`alg=none`, bounds sizes, rejects duplicate JSON fields, and can enforce
issuer, audience, and time policy. Typed claim readers avoid exposing the JSON
DOM as the token contract.

PASETO v4.public validation and v4.local seal/open are available when the
selected provider supports their algorithms. Verify footer and implicit
assertion policy; authenticated token syntax does not replace application
authorization.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| `UnsupportedAlgorithm` | Selected provider lacks the capability | Require capabilities at startup or configure a suitable provider |
| `AuthenticationFailed` | Wrong key/nonce/AAD/ciphertext or tampering | Reject output; do not retry by weakening policy |
| Secret appears in logs | Generic buffer/error logging crossed a secret boundary | Keep secrets in secret types and redact diagnostics |
| Parsed certificate accepted as trusted | Parsing confused with policy validation | Run explicit chain and identity validation |

**Source:** [`NGIN/Crypto`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto)

