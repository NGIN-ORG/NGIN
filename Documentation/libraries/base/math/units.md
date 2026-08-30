---
title: Dimensioned units
description: Use Unit, UnitCast, dimensions, ratios, and offset conversions to prevent mismatched quantities.
---

# Dimensioned units

`Unit<Exponents, ValueT, ConversionPolicy>` stores a numeric value plus a
compile-time SI dimension and conversion policy. Common aliases cover time,
length, mass, current, temperature, and derived velocity.

```cpp
using namespace NGIN::Units;

Milliseconds timeout{250.0};
Nanoseconds nanos = UnitCast<Nanoseconds>(timeout);
double raw = nanos.GetValue();
```

Addition/subtraction require compatible units. Multiplication/division compose
dimension exponents. `UnitCast<ToUnit>` performs a unit conversion;
`ValueCast<ToValueT>` changes representation while preserving dimension and
policy.

`RatioPolicy` represents scale-only conversions such as milliseconds to
seconds. `OffsetPolicy` and `FahrenheitToKelvinPolicy` handle temperature
origins. Offset units need special care in differences: an absolute temperature
and a temperature interval are different domain concepts even when the same
numeric storage is convenient.

Unit types prevent dimension mistakes at compile time, but they do not prevent
overflow, underflow, truncation, or loss of precision in `ValueT`. Converting a
fractional duration into an integral representation needs an explicit rounding
policy at the call boundary.

Use unit types in public APIs where a mismatch would be costly. Convert to raw
platform integers only at the narrow system call or wire-format boundary, and
document saturation/range behavior there.
