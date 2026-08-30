---
title: JSON parsing, building, and writing
description: Use strict JSON profiles, typed numeric access, duplicate-key policy, builders, and writers.
---

# JSON parsing, building, and writing

The default profile is strict: comments and trailing commas are rejected,
duplicate keys are rejected, UTF-8 and escapes are validated, and malformed
surrogate pairs fail.

## Parse policy

`JSON::ParseOptions` selects comment, trailing-comma, duplicate-key, and UTF-8
policy. Duplicate handling can reject, preserve, keep first, or keep last.
Choose it at the input boundary so downstream code does not silently interpret
ambiguous objects differently.

## Typed views

`ValueView` exposes its kind and checked `TryNull`/Boolean/numeric/string/array/
object conversions. Integers retain signed/unsigned 64-bit representation
rather than round-tripping through `double`; non-integral numbers must be
finite.

`ArrayView` iterates values. `ObjectView` iterates `MemberView` entries and
offers `Find`. These are borrowed and allocation-free; copy data that outlives
the document.

## Build

`JSON::Builder` creates immutable document state from values, arrays, and
objects. Builder failures report `BuildDiagnostic`/`BuildErrorCode`, including
resource/invalid handle/state problems. Finish once and transfer the resulting
document.

## Write

`JSON::Writer` serializes an existing view with `WriteOptions` and returns a
write diagnostic on invalid/output failure. `JSON::StreamWriter` authors output
incrementally through `TextSink`:

```cpp
std::string output;
JSON::StreamWriter writer {MakeTextSink(output)};
writer.BeginObject();
writer.Key("ready");
writer.Bool(true);
writer.EndObject();
auto result = writer.Finish();
```

Always check `Finish`: it detects incomplete nesting and deferred sink failure.

