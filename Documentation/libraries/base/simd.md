---
title: Learn NGIN.SIMD
description: Use portable lanes, masks, conversion/math policy, and byte scans while preserving scalar correctness.
---

# Learn NGIN.SIMD

NGIN.SIMD expresses the same operation over several lanes. It supplies a
portable scalar backend and compile-time platform backends; it does not perform
runtime CPU dispatch for arbitrary binaries.

## Start here

1. Learn [`Vec`, masks, backends, and policy tags](./simd/vectors-backends.md).
2. Use [byte scans and preserve tail/edge semantics](./simd/scans-correctness.md).
3. Look up exact declarations in the [SIMD API](../../reference/cpp/base/simd.md).

## Smallest shape

```cpp
#include <NGIN/SIMD.hpp>

using V = NGIN::SIMD::Vec<float>; // default configured backend/lane count

V a{1.0F};
V b{2.0F};
V c = a + b;
```

`Vec<T, Backend, Lanes>` encodes scalar type, backend tag, and lane count.
`Mask<Lanes, Backend>` represents lane predicates. `Select`, comparisons,
loads/stores, arithmetic, reductions, conversions, and selected math operations
compose over them.

Backend tags are `ScalarTag`, `SSE2Tag`, `AVX2Tag`, `AVX512Tag`, and `NeonTag`.
`DefaultBackend` comes from build configuration. A header being available does
not prove the running CPU supports instructions compiled into the binary.

## Correctness before speed

Define NaN, overflow, rounding, alignment, conversion, and partial-tail
behavior in scalar terms first. `StrictMathPolicy` and `FastMathPolicy` make
math tradeoffs visible. `ExactConversion`, `SaturateConversion`, and
`TruncateConversion` make lane conversion intent visible.

Profile the whole operation—including load, conversion, tail, and store—before
selecting SIMD. For short inputs, the scalar path may be faster and clearer.
