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

Backend tags are `ScalarTag`, `SSE2Tag`, `AVX2Tag`, `AVX512Tag`, and `NeonTag`.
Aliases `DefaultBackend` and `MathPolicy` come from configuration. Policy tags
are `StrictMathPolicy`, `FastMathPolicy`, `ExactConversion`,
`SaturateConversion`, and `TruncateConversion`.

## Scans

**Header:** `<NGIN/SIMD/Scan.hpp>`

`FindEqByte` accepts pointer/length or span and one target byte.
`FindAnyByte` overloads accept two, three, or four target bytes. They return the
first match index or `length` when absent. `Backend` is an optional template
argument, defaulting to `DefaultBackend`.

**Defined:** [`Vec.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/SIMD/Vec.hpp), [`Scan.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/SIMD/Scan.hpp)
