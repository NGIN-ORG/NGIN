---
title: Math and units
description: Use NGIN.Base vectors, matrices, transforms, geometry, big numbers, and dimensioned quantities.
---

# Math and units

## Start here

1. Learn [vectors and matrices](./math/linear-algebra.md).
2. Build [quaternions, transforms, projections, and geometry](./math/geometry-transforms.md).
3. Choose [BigInt or BigFloat](./math/big-numbers.md) for larger numeric domains.
4. Protect dimensioned APIs with [units and conversions](./math/units.md).
5. Look up declarations in the [Math and Units API](../../reference/cpp/base/math-units.md).

`NGIN/Units.hpp` provides compile-time dimensional quantities and unit
conversion. Arithmetic checks compatible dimensions at compile time and keeps
the representation type explicit. Prefer it at API boundaries where confusing
time, distance, rate, or storage units would be costly.

`NGIN/Math/Ratio.hpp` supplies ratio arithmetic used by conversion code.
`NGIN/Math/BigInt.hpp` provides large integer operations for workloads that
cannot fit the primitive integer domain. Use `BigInt::DivRem()` when both the
quotient and remainder are needed so division is performed once.

`NGIN/Math/Vector.hpp` and `NGIN/Math/Matrix.hpp` provide allocation-free,
fixed-size linear algebra values. Vectors support component arithmetic, dot and
three-dimensional cross products, lengths, distances, and checked or unchecked
normalization. Matrices use row-major storage and support rectangular
multiplication, column- and row-vector transforms, transpose, trace,
determinant, and inversion:

```cpp
using namespace NGIN::Math;

const Matrix3F transform {
        2.0F, 0.0F, 4.0F,
        0.0F, 3.0F, 5.0F,
        0.0F, 0.0F, 1.0F,
};
const Vector3F point {1.0F, 2.0F, 1.0F};
const Vector3F transformed = transform * point;

const Vector3F direction = Normalize(Vector3F {3.0F, 4.0F, 0.0F});
const Matrix3F inverse = Inverse(transform);
```

`Normalize()` and `Inverse()` are unchecked fast paths: their inputs must have
non-zero length and be non-singular, respectively. `TryNormalize()` returns no
value for vectors whose magnitude is within the requested tolerance of zero.
`TryInverse()` similarly returns no value for singular or
tolerance-degenerate matrices and uses pivoted elimination for robustness.
Matrix inversion requires a field-like component type; integer matrices support
element arithmetic, multiplication, and fraction-free determinant evaluation
but generally cannot represent an inverse.

The matrix storage contract is explicitly row-major. Both `matrix * vector`
(column-vector convention) and `vector * matrix` (row-vector convention) are
supported. Row-vector transforms naturally match the storage order, while
column-vector transforms preserve the conventional mathematical result without
changing the exposed layout.

## Linear algebra performance

`LinearAlgebraBenchmarks` compares representative vector and 4x4 matrix
operations against GLM 1.0.3. GLM is an optional benchmark-only dependency and
is never linked into NGIN.Base production targets. A native Release build can be
run with:

```bash
cmake -S . -B build/math-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DNGIN_BASE_BUILD_BENCHMARKS=ON \
  -DNGIN_BASE_BUILD_TESTS=OFF \
  -DNGIN_BENCH_USE_GLM=ON \
  -DNGIN_BENCH_USE_SIMDJSON=OFF \
  -DNGIN_BENCH_USE_RAPIDJSON=OFF \
  -DNGIN_BENCH_USE_PUGIXML=OFF \
  -DNGIN_BENCH_USE_TINYXML2=OFF \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -march=native'
cmake --build build/math-bench --target LinearAlgebraBenchmarks
./build/math-bench/benchmarks/LinearAlgebraBenchmarks
```

On an Intel Core i7-13700KF, repeated pinned-core runs with GCC 15.2 placed
vector arithmetic, normalization, matrix-matrix multiplication, and 4x4
inversion within roughly 2% of GLM. The row-major `vector * matrix` path was
about 2.5 times as fast as GLM, while GLM's column-major `matrix * vector` path
was about twice as fast as NGIN. Across the ten paired operations, the
representative geometric-mean NGIN/GLM ratio was 0.98. With Clang 20.1, NGIN
matched or beat GLM on every matrix operation except inversion (within 2%) and
had a geometric-mean ratio of 0.85. Lower ratios are better. These measurements
are reference evidence, not performance guarantees; rerun the benchmark for the
target compiler, flags, CPU, and chosen vector convention.

`NGIN/Math/BigFloat.hpp` provides deterministic arbitrary-precision binary
floating point with a compile-time significand precision. It is intended for
research, simulation, and other numerical work that needs more precision than
the primitive floating-point types while retaining familiar arithmetic. Its
normalized binary exponent range is -1,000,000 through +1,000,000:

```cpp
using Real = NGIN::Math::BigFloat<256>;

const Real position("1.0");
const Real velocity("0.125");
const Real timestep("0.001");
const Real next = NGIN::Math::Fma(velocity, timestep, position);
```

The default policy rounds to nearest with ties to even. Alternative directed
rounding policies are selected as the second template argument. A value carries
signed zero, infinity, and quiet NaN, but rounding state is never global or
thread-local. `Sqrt()` and `Fma()` perform one final rounding, and
`ToHexString()` provides an exact, parseable representation for diagnostics.
Decimal `ToString()` falls back to this hexadecimal representation for binary
exponents beyond its bounded decimal-conversion window, avoiding unexpectedly
large temporary allocations.

Choose representation types according to range and precision requirements;
unit typing prevents dimensional mistakes but cannot prevent numeric overflow.
For timeouts and scheduling, follow the duration types accepted by the specific
Execution, IO, or Net API rather than converting through an untyped integer.
