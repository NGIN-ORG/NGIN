---
title: SIMD scans and correctness
description: Find delimiter bytes efficiently while preserving short-input, tail, and not-found semantics.
---

# SIMD scans and correctness

`FindEqByte` searches for one byte. `FindAnyByte` has overloads for two, three,
or four byte values. Each accepts pointer/length or `std::span` forms and returns
the matching index, or the input length when no value is found.

The `FindEqByteRuntime` and `FindAnyByteRuntime` forms keep the same contract but
select once per process among the separately compiled scalar, SSE2, AVX2,
AVX-512, and Neon kernels available in the linked Foundation library.

```cpp
std::span<const char> input = GetInput();
const std::size_t end = NGIN::SIMD::FindAnyByteRuntime(input, '\r', '\n');
if (end == input.size()) {
    // No line ending.
}
```

The implementation uses a scalar path for small inputs, vector lanes for the
bulk, and a safe tail. Callers should rely on the semantic result—not on a
specific instruction threshold or backend.

## Preserve the scalar contract

When writing other vectorized loops:

1. define one scalar operation for every valid input;
2. process only complete readable vector ranges;
3. handle remaining elements without out-of-bounds access;
4. preserve first-match/order behavior where it matters;
5. compare scalar and vector output across random and adversarial inputs.

Benchmark representative buffer sizes. A vectorized loop can lose on short
data or when setup, conversion, or memory bandwidth dominates. Use sanitizer
and boundary tests around zero length, one byte, lane-size minus/at/plus one,
unaligned starts, no match, and a match in every possible tail position.
