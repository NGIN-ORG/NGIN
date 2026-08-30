---
title: Vectors and matrices
description: Work with fixed-size vectors and row-major matrices while making normalization, inversion, and convention explicit.
---

# Vectors and matrices

`Vector<T, N>` and `Matrix<T, Rows, Columns>` are fixed-size, allocation-free
values. Common aliases such as `Vector3F` and `Matrix4F` select familiar scalar
types and dimensions.

```cpp
using namespace NGIN::Math;

Vector3F direction{3.0F, 4.0F, 0.0F};
auto unit = TryNormalize(direction);
if (!unit)
    return HandleZeroDirection();

float alignment = Dot(*unit, Vector3F{1.0F, 0.0F, 0.0F});
```

Vectors provide component arithmetic, dot product, 3D cross product, length,
distance, and normalization. `Normalize` is an unchecked fast path requiring
non-zero magnitude. `TryNormalize` returns no value within its zero tolerance.

Matrices use row-major storage. Rectangular multiplication, transpose, trace,
determinant, matrix/vector multiplication, and inversion are available where
the scalar/dimensions support them. Both `matrix * vector` (column-vector
convention) and `vector * matrix` (row-vector convention) exist; select and
document one convention at system boundaries.

`Inverse` requires a non-singular matrix and a field-like scalar type.
`TryInverse` detects singular/tolerance-degenerate input using pivoting. Integer
matrices support much of the arithmetic and determinant work but generally
cannot represent an inverse.

Tolerance is domain policy. A threshold appropriate for normalized transforms
may be wrong for very large or very small physical values. Pass or document it
rather than relying blindly on a generic default.
