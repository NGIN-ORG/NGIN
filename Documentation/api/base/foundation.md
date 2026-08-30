---
title: NGIN.Base Foundation API
description: Primitives, expected results, exceptions, text, Unicode, math, units, time, SIMD, metadata, hashing, and utilities.
---

# NGIN.Base Foundation API

**Include:** `<NGIN/NGIN.hpp>` or a focused umbrella  
**Target:** `NGIN::Base::Foundation`

Foundation contains the provider-free APIs used by every other NGIN.Base
component. `<NGIN/NGIN.hpp>` includes Foundation only; it intentionally does
not pull in Async, Execution, I/O, Networking, Serialization, or Crypto.

## API map

| Area | Include | Main APIs |
| --- | --- | --- |
| Scalars/platform | `<NGIN/Primitives.hpp>`, `<NGIN/Defines.hpp>` | Fixed-width aliases, platform and visibility declarations |
| Results/errors | `<NGIN/Utilities.hpp>`, `<NGIN/Exceptions.hpp>` | `Expected`, `Unexpected`, `Optional`, `ErrorInfo`, exception hierarchy |
| Text/Unicode | `<NGIN/Text.hpp>` | `BasicString`, `String`, UTF views, validation, conversion, error policy |
| Math/units | `<NGIN/Math.hpp>`, `<NGIN/Units.hpp>` | vectors, matrices, transforms, geometry, interpolation, ratios, units |
| Time | `<NGIN/Time.hpp>` | `MonotonicClock`, `TimePoint`, sleep helpers |
| SIMD | `<NGIN/SIMD.hpp>` | vector lanes, tags, scanning, backend selection |
| Metadata | `<NGIN/Meta.hpp>` | `TypeId`, `SymbolId`, type names and traits, reflection identity |
| Hashing | `<NGIN/Hashing.hpp>` | FNV, CRC, checksum APIs |
| Type erasure/symbols | `<NGIN/Utilities.hpp>` | `Any`, `Callable`, `StringInterner`, `SymbolTable` |

Memory and containers are Foundation-owned but have a separate
[reference page](./memory-containers.md).

## Results and exceptions

Use `Expected<T, E>` when the caller can act on a domain failure and
`Optional<T>` when absence has no additional reason. Prefer the owning
subsystem's typed error over erasing it to `ErrorInfo` too early.

An expected-returning API is not automatically `noexcept`; allocation or user
callbacks can still throw where documented. Check the called declaration.

`Exception` can capture a stack trace at construction when
`NGIN_BASE_CAPTURE_EXCEPTION_STACKTRACE=ON`. The option defaults off because
capture can allocate and increase construction latency.

## Text and Unicode

`BasicString` is an allocator-aware owning string. Unicode APIs operate on
explicit UTF-8, UTF-16, and UTF-32 code-unit forms and expose validation and
conversion error policy.

Keep these layers separate:

- byte count is not code-point count;
- code-point count is not displayed grapheme count;
- valid UTF is not necessarily normalized text;
- string views borrow their source.

Validate untrusted encoded input before algorithms that assume valid code-unit
sequences. Choose strict rejection or a documented replacement policy at the
boundary instead of silently changing behavior inside business logic.

## Math and units

Math includes vector, matrix, affine matrix, quaternion, transform, geometry,
projection, decomposition, and interpolation APIs plus large integer/float
support. Templates encode scalar and dimension where applicable.

Units uses strongly typed dimensions and `UnitCast`:

```cpp
auto milliseconds = NGIN::Units::Milliseconds {250.0};
auto nanoseconds =
    NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(milliseconds);
```

Do not mix coordinate handedness, row/column conventions, angle units, or
projection depth ranges implicitly. State the convention at API boundaries.
Check divide-by-zero, singular-matrix, normalization, overflow, and precision
behavior in the specific operation you call.

## Time

Use `MonotonicClock` for elapsed time, deadlines, and scheduling. It does not
represent civil/calendar time and is not stable across machines or process
runs. `TimePoint` stores monotonic nanoseconds; unit values make durations
explicit.

## SIMD

`SIMD::Vec` and scan operations select supported backend behavior through
configuration and tags. Do not assume a particular instruction set merely
because the compiler can emit it. Preserve a correct scalar/portable path and
measure the workload before introducing SIMD-specific complexity.

## Metadata and identity

`TypeId`, `SymbolId`, and `ReflectionIdentity` provide NGIN-facing identities.
`TypeName` is compiler-derived diagnostic text. Never persist a `TypeName` as a
wire-format or storage-schema identity; use an explicit stable symbol/schema
identifier.

`FunctionTraits`, `TypeTraits`, and `EnumTraits` support constrained generic
code. They describe compile-time shape and do not replace the runtime registry
in NGIN.Reflection.

## Hashing

- FNV is suitable for stable non-cryptographic identifiers and hash tables.
- CRC/checksums detect accidental corruption.
- Neither authenticates attacker-controlled data. Use Crypto MAC or signature
  APIs for integrity against an attacker.

## Utility ownership

`Any` owns a type-erased value. `Callable` owns or stores callable state
according to its construction path. `StringInterner` and `SymbolTable` keep
storage that backs returned identifiers/views; document their lifetime when
those values cross a boundary.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| Garbled/truncated Unicode | Byte indexing treated as character indexing | Use the UTF view/conversion APIs |
| Persisted type ID changes by compiler/build | Diagnostic type name was persisted | Define an explicit schema identity |
| Integrity check is forgeable | FNV/CRC used as authentication | Use a keyed MAC or signature |
| Deadline behaves like wall-clock time | Monotonic and civil time mixed | Use monotonic only for intervals/deadlines |

**Source:** [`NGIN.Base Foundation headers`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN)

