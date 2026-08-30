---
title: Serialization parsing core
description: Code reference for input ownership, scratch/resources, limits, errors, and source spans.
---

# Serialization parsing core

## Source buffers

**Header:** `<NGIN/Serialization/Core/SourceBuffer.hpp>`

| Type | Contract |
| --- | --- |
| `OwnedTextBuffer` | Owns immutable source bytes transferred into a document/parser |
| `MutableTextBuffer` | Owns writable bytes that in-situ parsing may decode/normalize |
| `BorrowedTextView` | Borrows immutable bytes; owner must outlive parse result/views |

A plain `std::string_view` is intentionally not accepted as an owning input.

## Limits and resources

`ParseLimits` bounds input, nesting, node/member counts, decoded text, and total
retained allocation. `ParseScratch` owns reusable temporary parsing capacity.
`ParseResources` groups allocator/resource policy used by parser/document state.
Exact fields/defaults are declared in their focused headers.

## Errors

```cpp
enum class ParseErrorCode : UInt8;

struct ParseError {
    ParseErrorCode code;
    ParseLocation location;
    SourceSpan span;
    std::optional<SourceSpan> related;
    UInt64 consumerContext;
    Text::String message;
};

using ParseDiagnostic = ParseError;
```

Codes cover unexpected input/end, tokens, numeric/string/entity/encoding
errors, depth/limit/memory failures, duplicates, mismatched tags, unsupported
constructs, and invalid document structure.

## Source information

`SourceSpan` identifies a byte range and source ID. `SourceLocation` adds
line/column mapping. `SourceMap` translates offsets/spans for diagnostics.
Document node views expose their source spans where supported.

## Lifetime

Borrowed input, scratch, document state, and views each have separate roles.
Consult the chosen parse overload: a scratch arena being reusable after parse
does not imply that a borrowed input may be destroyed.

