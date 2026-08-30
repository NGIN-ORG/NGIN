---
title: Math and units API
description: API reference for linear algebra, geometry/transforms, large numbers, ratios, and dimensioned quantities.
---

# Math and units API

## Fixed-size math

**Header:** `<NGIN/Math.hpp>`

`Vector<T, N>` and `Matrix<T, Rows, Columns>` provide allocation-free fixed
storage, component arithmetic, comparisons, and linear algebra. Free operations
include dot/cross, length/distance, checked/unchecked normalization, transpose,
trace, determinant, checked/unchecked inverse, and rectangular multiplication.
Matrices expose row-major storage and both row-/column-vector multiplication.

Focused headers add `Quaternion`, `Transform`, `AffineMatrix`, projections,
geometry primitives/intersections, decomposition, and interpolation.

## Large numbers

`BigInt` is a signed arbitrary-size integer with arithmetic, shifts, bit
operations, conversions, formatting, and `DivRem`. `BigFloat<PrecisionBits,
RoundingMode>` is a bounded-exponent arbitrary-precision binary float with
special values, parse/format, arithmetic, `Sqrt`, `Fma`, and numeric-limits
support.

## Units

**Header:** `<NGIN/Units.hpp>`

`QuantityExponents` stores seven SI base-dimension exponents.
`Unit<Q, ValueT, Policy>` stores a typed quantity. `UnitCast<ToUnit>` converts
units; `ValueCast<ToValueT>` changes numeric representation. `RatioPolicy`,
`OffsetPolicy`, and `FahrenheitToKelvinPolicy` define conversion behavior.
Aliases include time, length, mass, current, temperature, and velocity units.

[Browse Math headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Math) and [`Units.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Units.hpp).
