---
title: Big integers and floats
description: Choose BigInt or fixed-precision BigFloat and define parsing, rounding, range, and diagnostic policy.
---

# Big integers and floats

Use `BigInt` when integer range can exceed primitive widths. It supports signed
arithmetic, comparison, shifts, bit operations, conversion, and textual forms.
Use `DivRem` when both quotient and remainder are required so division is
performed once.

Use `BigFloat<PrecisionBits, RoundingMode>` when a fixed compile-time binary
precision must exceed primitive floating-point precision.

```cpp
using Real = NGIN::Math::BigFloat<256>;

Real position{"1.0"};
Real velocity{"0.125"};
Real dt{"0.001"};
Real next = NGIN::Math::Fma(velocity, dt, position);
```

The default rounding mode is nearest with ties to even. Alternative directed
rounding modes are template policy, not process-global state. `Sqrt` and `Fma`
perform one final rounding. Values include signed zero, infinity, and quiet NaN;
the normalized exponent range is bounded.

`ToHexString` is exact and parseable. Decimal formatting may fall back to a
hexadecimal representation for extreme binary exponents to avoid enormous
temporary allocations. Treat text format as a versioned persistence decision
when external compatibility matters.

Arbitrary precision means “chosen larger precision/range,” not infinite
accuracy or constant cost. Operation time and storage grow with operand size.
Bound untrusted textual inputs and choose precision from error analysis, not
from an arbitrary impressive number.
