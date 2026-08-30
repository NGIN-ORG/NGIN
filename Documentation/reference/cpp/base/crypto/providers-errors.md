---
title: Crypto providers and errors
description: API reference for CryptoContext selection, capabilities, expected results, and failure codes.
---

# Crypto providers and errors

## Result types

**Headers:** `<NGIN/Crypto/Result.hpp>`, `<NGIN/Crypto/Errors.hpp>`

```cpp
template<class T>
using CryptoExpected = std::expected<T, CryptoError>;
```

`CryptoError` exposes `Code()`, `PlatformCode()`, `HasError()`, and
`Message()`. `CryptoErrorCode` contains `InvalidArgument`,
`OutputBufferTooSmall`, `InvalidKey`, `InvalidNonce`, `InvalidTag`,
`AuthenticationFailed`, `UnsupportedAlgorithm`, `UnsupportedBackend`,
`BackendUnavailable`, `EntropyUnavailable`, `EncodingError`, `ParseError`,
`PolicyRejected`, and `InternalError` in addition to `None`.

## `BackendOptions`

**Header:** `<NGIN/Crypto/Backend/BackendOptions.hpp>`

Fields are `requireSecureRandom`, `policy`, `requiredAlgorithms`, and
`packageName`. `BackendPolicy` supports platform/package restriction,
preference ordering, FIPS-capable selection, and required algorithm sets.

## Context factories

```cpp
CryptoExpected<CryptoContext> CreateContext(const BackendOptions& = {}) noexcept;
BackendContextSelection CreateContextWithDiagnostics(const BackendOptions& = {}) noexcept;
CryptoExpected<CryptoContext> CreateBestAvailableContext() noexcept;
CryptoExpected<CryptoContext> CreatePlatformContext() noexcept;
CryptoExpected<CryptoContext> CreatePackageContext(std::string_view) noexcept;
```

## `CryptoContext`

`Info()` and `Capabilities()` describe the selected backend. `Supports(...)`,
`DescribeSupport(...)`, and `EnsureSupports(...)` overload across hash, MAC,
KDF, AEAD, key-agreement, asymmetric-encryption, and signature algorithms.
`SupportsRandom()` and `DescribeRandomSupport()` cover entropy separately.

The context also owns low-level `...Into` provider dispatch used by the
algorithm wrappers. It is explicit immutable-capability state; there is no
public mutable global provider registry.

**Defined:** [`CryptoContext.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Crypto/Backend/CryptoContext.hpp)
