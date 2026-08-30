---
title: Text and Unicode
description: Use NGIN.Base owned strings, views, encodings, and Unicode conversion and validation.
---

# Text and Unicode

## Start here

1. Learn [`BasicString`, aliases, views, and allocator behavior](./text/basic-string.md).
2. Learn [validation, conversion, code points, and error policy](./text/unicode.md).
3. Look up declarations in the [Text API](../../reference/cpp/base/text.md).

`NGIN.Base` splits text storage from encoding policy on purpose.

- `NGIN::Text::BasicString` is a fast allocator-aware container of code units.
- `NGIN::Text::Unicode` handles validation, decoding, encoding, conversion, and UTF-8 code-point iteration.

Use this split when you want predictable low-level string behavior without hiding Unicode costs behind ordinary string operations.

## `BasicString`

`NGIN::Text::BasicString<CharT, SBOBytes, Alloc, Growth, Traits>` provides:

- small-buffer optimization sized in bytes
- allocator-backed growth
- traits-aware comparison and search
- contiguous iterators over the stored code units
- a null-termination invariant for interop

Important semantics:

- `Size()` counts code units, not code points or grapheme clusters
- `operator[]`, `At()`, `Substr()`, `Find()`, and `RFind()` are code-unit based
- UTF aliases such as `UTF8String` and `UTF16String` do not imply validation
- `UTF8String` stores `char8_t`; use `AsBytes()` for a zero-copy byte view and
  `UTF8FromBytes()` for an explicit copy from byte-oriented text

Common aliases are declared in
[`NGIN/Text/String.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Text/String.hpp):

- `String`
- `UTF8String`
- `UTF16String`
- `UTF32String`

## Unicode Utilities

`NGIN::Text::Unicode` provides the encoding-aware layer:

- validation: `IsValidUtf8`, `IsValidUtf16`, `IsValidUtf32`
- decode/encode primitives: `DecodeUtf8`, `DecodeUtf16`, `EncodeUtf8`, `EncodeUtf16`, `EncodeUtf32`
- conversion: `ToUtf8`, `ToUtf16`, `ToUtf32`
- helpers: `CountCodePoints`, `IsAscii`, `DetectBom`, `StripBom`
- UTF-8 iteration: `Utf8View`

The shared Unicode error model is:

- `ErrorPolicy::Strict`
- `ErrorPolicy::Replace`
- `ErrorPolicy::Skip`

Strict-mode APIs return `NGIN::Utilities::Expected<..., ConversionError>` so the caller can keep failure handling explicit.

## Typical Usage

```cpp
#include <NGIN/Text/String.hpp>
#include <NGIN/Text/Unicode.hpp>

NGIN::Text::UTF8String utf8(u8"Hello 😀");

auto utf16 = NGIN::Text::Unicode::ToUtf16(utf8.View());
if (!utf16.has_value())
{
    return;
}

for (NGIN::Text::Unicode::CodePoint cp: NGIN::Text::Unicode::Utf8View(utf8.View()))
{
    (void)cp;
}
```

## When To Use Which Layer

- Use `BasicString` when you need storage, mutation, search, and interop over known code-unit data.
- Use `Unicode` when input may be malformed, when converting between encodings, or when iterating UTF-8 by code point.
- Do not expect filesystem or whole-file helpers to pick an encoding policy for you; choose and apply one explicitly.
