---
title: Providers, capabilities, and errors
description: Select an NGIN.Crypto backend explicitly and preserve useful failure information.
---

# Providers, capabilities, and errors

NGIN.Crypto exposes one provider-neutral API, but the selected backend decides
which algorithms are actually available. Treat provider selection as an
application startup decision—not as an implementation detail discovered on the
first request.

## Create a context

```cpp
#include <NGIN/Crypto.hpp>

auto selected = NGIN::Crypto::Backend::CreateBestAvailableContext();
if (!selected)
    return selected.error();

auto context = std::move(selected).value();
```

`CreatePlatformContext()` restricts selection to platform facilities.
`CreatePackageContext("openssl")` requests a named package provider. For
policy-driven selection, use `CreateContext(BackendOptions)` and choose a
`BackendPolicy` such as `PlatformOnly`, `PackagesOnly`, or
`RequireAlgorithmSet`.

## Require what the application needs

```cpp
auto required = NGIN::Crypto::Backend::AlgorithmSet{}
    .Require(NGIN::Crypto::HashAlgorithm::Sha256)
    .Require(NGIN::Crypto::AeadAlgorithm::Aes256Gcm);

auto selected = NGIN::Crypto::Backend::CreateContext({
    .policy = NGIN::Crypto::Backend::BackendPolicy::RequireAlgorithmSet,
    .requiredAlgorithms = required,
});
```

For optional features, call `context.Supports(algorithm)`. Use
`DescribeSupport(algorithm)` when a diagnostic must explain why a feature is
unavailable. `CreateContextWithDiagnostics()` preserves every rejected
candidate and is the better startup-tooling API.

## Handle recoverable failures

Crypto operations return `CryptoExpected<T>`, whose error is `CryptoError`.
Inspect `Code()` for portable behavior, `PlatformCode()` only for backend
diagnostics, and `Message()` for a static explanation.

`AuthenticationFailed` is materially different from
`UnsupportedAlgorithm`: the former means output must be rejected, while the
latter usually indicates a deployment or capability-policy mismatch. Never
log key material, plaintext, passwords, or full token contents while reporting
either error.

## Design rule

Build one context with an explicit policy, verify required capabilities once,
and pass that context to operations. This makes production behavior testable
and prevents an accidental provider change from silently changing the
application's security contract.

Next: [secure random values and secrets](./random-secrets.md), or inspect
[provider and error declarations](../../../reference/cpp/base/crypto/providers-errors.md).
