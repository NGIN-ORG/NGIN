---
title: Geometry and transforms
description: Compose quaternions, affine transforms, projections, primitives, decomposition, and interpolation with explicit conventions.
---

# Geometry and transforms

NGIN.Math layers geometry and transform operations over its fixed-size vector
and matrix values.

| Header | Responsibility |
| --- | --- |
| `Quaternion.hpp` | Rotation representation and composition |
| `Transform.hpp` | Translation/rotation/scale composition |
| `AffineMatrix.hpp` | Affine matrix forms |
| `Projection.hpp` | Perspective/orthographic projections |
| `Geometry.hpp` | Rays, planes, bounds, and intersection helpers |
| `Decomposition.hpp` | Recover transform parts from matrices |
| `Interpolation.hpp` | Scalar/vector/quaternion interpolation |

Before exchanging transform data, state:

- left- or right-handed coordinates;
- row- or column-vector multiplication convention;
- matrix storage order (NGIN matrices are row-major);
- angle unit;
- quaternion component ordering;
- projection depth range and clip-space convention.

A mathematically correct matrix under one convention can be visibly wrong
under another. Keep conversions at named boundaries instead of sprinkling
transposes or sign changes through call sites.

Normalize rotation inputs where required. Decomposition can fail or become
unstable for singular, sheared, or tolerance-degenerate transforms; use the
checked operation when input is not guaranteed by construction.

For collision/intersection helpers, decide whether touching counts as an
intersection and what epsilon means in world units. For interpolation, decide
whether extrapolation is allowed and whether quaternion interpolation should
take the shortest arc. These are domain semantics, not merely performance
details.
