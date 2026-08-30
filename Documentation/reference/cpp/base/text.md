---
title: Text and Unicode API
description: API reference for BasicString aliases, code-unit storage, UTF validation/conversion, policies, and views.
---

# Text and Unicode API

## `BasicString`

**Headers:** `<NGIN/Text/BasicString.hpp>`, `<NGIN/Text/String.hpp>`

`BasicString<CharT, SBOBytes, Alloc, Growth, Traits>` is an allocator-aware
contiguous owning string with byte-sized SBO and a null-termination invariant.
Its construction, assignment, append/insert/erase, capacity, iterators, indexed
access, substrings, compare/search, `View`, and byte-view operations are
code-unit based.

Aliases are `String`, `UTF8String`, `UTF16String`, and `UTF32String`.
`UTF8FromBytes` copies byte-oriented UTF-8 into `char8_t` storage; `AsBytes`
provides a borrowed byte view.

## Unicode

**Header:** `<NGIN/Text/Unicode.hpp>`

| Area | Symbols |
| --- | --- |
| Validation | `IsValidUtf8`, `IsValidUtf16`, `IsValidUtf32` |
| Decode/encode | `DecodeUtf8`, `DecodeUtf16`, `EncodeUtf8`, `EncodeUtf16`, `EncodeUtf32` |
| Convert | `ToUtf8`, `ToUtf16`, `ToUtf32` |
| Inspect | `CountCodePoints`, `IsAscii`, `DetectBom`, `StripBom` |
| Iterate | `Utf8View`, `CodePoint` |
| Policy/errors | `ErrorPolicy::{Strict, Replace, Skip}`, `ConversionError` |

Strict conversions return `Utilities::Expected<..., ConversionError>`.
`Utf8View` borrows its source and iterates code points, not grapheme clusters.

[Browse Text headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Text).
