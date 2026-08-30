---
title: Unicode validation and conversion
description: Validate UTF, convert encodings, iterate code points, and choose strict, replacement, or skip policy.
---

# Unicode validation and conversion

NGIN.Text.Unicode separates encoding correctness from string storage.

```cpp
auto converted = NGIN::Text::Unicode::ToUtf16(utf8.View());
if (!converted)
    return converted.error();
```

Validation functions are `IsValidUtf8`, `IsValidUtf16`, and `IsValidUtf32`.
Decode/encode primitives operate one code point at a time. `ToUtf8`, `ToUtf16`,
and `ToUtf32` convert complete inputs. `CountCodePoints`, `IsAscii`, `DetectBom`,
and `StripBom` support common boundaries.

`ErrorPolicy` has three modes:

- `Strict` rejects malformed input with `ConversionError`;
- `Replace` emits the Unicode replacement character;
- `Skip` drops invalid sequences.

Choose at the input boundary. Strict is appropriate for protocols and formats
where silent modification is unsafe. Replacement is useful for user-visible
best-effort display. Skip loses evidence and should be used only by explicit
policy.

`Utf8View` iterates decoded `CodePoint` values over UTF-8. Its source is
borrowed, and code-point iteration still is not grapheme segmentation or
locale-aware text processing. Unicode normalization, case folding, and display
width are separate concerns unless an API explicitly documents them.

When reporting conversion failure, retain the source offset and selected
policy. Do not index UTF-8 by byte and call the result a character position.
