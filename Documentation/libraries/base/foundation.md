---
title: Learn the NGIN.Base foundation
description: Navigate the provider-free types shared by every other NGIN.Base library.
---

# Learn the NGIN.Base foundation

Foundation is the common vocabulary beneath the compiled NGIN.Base libraries.
It contains primitive aliases, typed result and utility types, text, math,
units, monotonic time, SIMD, metadata, and non-cryptographic hashing.

`<NGIN/NGIN.hpp>` includes this foundation surface. It intentionally does not
pull in Async, Execution, I/O, Networking, Serialization, or Crypto.

## Choose a path

| You need to… | Start here |
| --- | --- |
| Represent recoverable failure or absence | [Exceptions and typed results](./exceptions-results.md) |
| Identify types/symbols or hash non-secret data | [Meta and hashing](./meta-hashing.md) |
| Store type-erased values/callables or intern names | [Utilities](./utilities.md) |
| Own strings, validate UTF, or convert encodings | [Text and Unicode](./text.md) |
| Use vectors, transforms, geometry, precision, or dimensions | [Math and units](./math-units.md) |
| Measure elapsed time or sleep | [Time](./time.md) |
| Vectorize a measured data-parallel operation | [SIMD](./simd.md) |
| Allocate memory or choose a container | [Memory and containers](./memory-containers.md) |

## The important boundaries

Foundation APIs are low-level, but they are not interchangeable conveniences:

- `Expected<T, E>` explains failure; `Optional<T>` represents reasonless absence.
- `String` owns code units; views borrow, and code units are not Unicode characters.
- `TypeName` is compiler-derived diagnostic text, not a stable schema identity.
- FNV and CRC are not cryptographic authentication.
- `TimePoint` belongs to one unspecified monotonic epoch, not wall-clock time.
- `SIMD::Vec` preserves a scalar contract; backend availability is build-dependent.
- unit typing prevents dimension mismatch, not numeric overflow or loss of precision.

## Reading reference pages

Learning pages explain when and why to use a facility. The
[Foundation C++ API](../../reference/cpp/base/foundation.md) is organized by
headers and symbols for exact declarations. Every detailed reference links to
the owning public headers in the repository.

Prefer focused includes in public headers. Treat only documented public
namespaces as supported contracts; a reachable `detail` symbol remains an
implementation detail.
