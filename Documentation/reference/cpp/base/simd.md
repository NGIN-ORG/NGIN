---
title: SIMD API
description: API reference for Vec, Mask, backend tags, conversion/math policies, selection, and byte scans.
---

# SIMD API

**Header:** `<NGIN/SIMD.hpp>`  
**Namespace:** `NGIN::SIMD`

```cpp
template<class T, class Backend = NGIN_SIMD_DEFAULT_BACKEND, int Lanes = -1>
struct Vec;

template<int Lanes, class Backend = NGIN_SIMD_DEFAULT_BACKEND>
struct Mask;
```

`Vec` exposes resolved `lanes`, lane/scalar types, load/store, indexed/lane
access, arithmetic/bitwise operations where supported, comparisons to masks,
reductions, conversions, and selected math functions. `Select(mask, a, b)`
combines values. `BitCast` preserves bits across supported equal-size vector
forms.

Supported backend tags are `ScalarTag`, `SSE2Tag`, `AVX2Tag`, `AVX512Tag`, and
`NeonTag`. AVX-512 native-width operations require AVX-512F, BW, DQ, and VL.
Aliases `DefaultBackend` and `MathPolicy` come from configuration. Policy tags
are `StrictMathPolicy`, `FastMathPolicy`, `ExactConversion`,
`SaturateConversion`, and `TruncateConversion`.

## Scans

**Header:** `<NGIN/SIMD/Scan.hpp>`

`FindEqByte` accepts pointer/length or span and one target byte.
`FindAnyByte` overloads accept two, three, or four target bytes. They return the
first match index or `length` when absent. `Backend` is an optional template
argument, defaulting to `DefaultBackend`.

`FindEqByteRuntime` and `FindAnyByteRuntime` expose the same pointer/span and
one-byte-type forms, but resolve the best linked kernel for the current CPU and
OS state. They require the Foundation library rather than being header-only.

## Runtime selection

**Header:** `<NGIN/SIMD/Runtime.hpp>`

`RuntimeFeatures` records usable SSE2, AVX2, AVX-512F/BW/DQ/VL, and Neon
features. `DetectRuntimeFeatures` performs an uncached query;
`GetRuntimeFeatures` returns the process cache. `GetCompiledBackends` reports
linked variants and `GetRuntimeBackend` returns the highest usable one.

`RuntimeDispatchTable<FunctionPointer>` stores scalar, SSE2, AVX2, AVX-512, and
Neon function variants. `Resolve()` returns the highest supported non-null
variant, preserving the scalar fallback.

**Defined:** [`Vec.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/SIMD/Vec.hpp), [`Scan.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/SIMD/Scan.hpp), [`Runtime.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/SIMD/Runtime.hpp)
