---
title: SIMD vectors and backends
description: Select lane types, masks, configured backends, conversion policy, and math policy.
---

# SIMD vectors and backends

`Vec<T, Backend, Lanes>` stores a compile-time lane group. `Lanes = -1` selects
the native lane count for the backend/scalar pair. `Mask<Lanes, Backend>` stores
comparison results, and `Select` chooses lane values from a mask.

Supported backend tags are scalar, SSE2, AVX2, AVX-512, and Neon. AVX-512 is
enabled only when AVX-512F, BW, DQ, and VL are all available to the translation
unit. `DefaultBackend` is set by `NGIN_SIMD_DEFAULT_BACKEND`; it is a
compile/build choice.

For one binary that must run on different CPUs, use separately compiled
functions and `RuntimeDispatchTable`. Runtime detection checks CPU and operating
system vector state, then resolves AVX-512, AVX2, SSE2, Neon, or scalar in that
order. The linked Foundation library uses this mechanism for the
`Find*ByteRuntime` scans. Dispatch selects functions; it does not give
`Vec<T>` a dynamic-width ABI.

Native-width backend operation sets override the scalar reference implementation
where the façade has a dedicated intrinsic path. Masks use a packed bit
representation, allowing compare results and `MaskToBits` to lower without a
per-lane Boolean round trip. Other lane counts and individual operations can use
scalar fallback code, so a backend tag alone is not a performance guarantee.

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
