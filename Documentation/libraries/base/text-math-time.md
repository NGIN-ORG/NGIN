---
title: Text, math, and time
description: Work with Unicode text, numeric utilities, units, clocks, durations, and SIMD facilities.
---

# Text, math, and time

These subsystems provide reusable value types and operations that avoid common
ambiguity at API boundaries.

## Text

Text APIs distinguish Unicode text from arbitrary bytes. State the expected
encoding when reading external data, and avoid indexing Unicode text as though
every user-visible character were one byte or one code point.

## Math and units

Math facilities cover numeric and geometry-oriented utilities. Unit-aware
types make dimensions explicit and prevent accidental mixing of quantities
that share the same underlying scalar.

## Time

Use monotonic time for elapsed durations and deadlines. Use wall-clock time for
calendar or externally meaningful timestamps. A wall clock can jump; a
monotonic clock cannot be converted into a calendar timestamp without another
reference.

## SIMD

SIMD facilities provide vectorized operations where the supported platform and
data layout justify them. Keep a correct scalar contract and measure the actual
workload before selecting a specialized path.
