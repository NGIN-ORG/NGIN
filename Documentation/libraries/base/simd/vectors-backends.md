---
title: SIMD vectors and backends
description: Select lane types, masks, configured backends, conversion policy, and math policy.
---

# SIMD vectors and backends

`Vec<T, Backend, Lanes>` stores a compile-time lane group. `Lanes = -1` selects
the native lane count for the backend/scalar pair. `Mask<Lanes, Backend>` stores
comparison results, and `Select` chooses lane values from a mask.

Backend tags are scalar, SSE2, AVX2, AVX-512, and Neon. `DefaultBackend` is set
by `NGIN_SIMD_DEFAULT_BACKEND`; it is a compile/build choice. If one binary must
run on CPUs with different capabilities, provide external runtime dispatch
between separately compiled compatible implementations.

Loads and stores must obey the exact aligned/unaligned declaration used. Do not
cast an arbitrary pointer to a wider vector type and assume alignment or aliasing
is valid. Handle complete lanes first and process the tail with a scalar or
masked path supported by the API.

## Conversion and math policy

`ExactConversion`, `SaturateConversion`, and `TruncateConversion` specify what
happens when lane values change scalar type. “Exact” is a contract, not a claim
that every input is representable; inspect preconditions/failure behavior of
the selected conversion.

`StrictMathPolicy` prioritizes standard scalar semantics.
`FastMathPolicy` permits approximations documented by the implementation.
Choose policy from error tolerance and special-value requirements, then test
NaN, infinity, signed zero, and range boundaries.
