---
title: Serialization ownership, views, limits, and diagnostics
description: Choose owned, borrowed, in-situ, or lossless parsing and preserve every backing lifetime.
---

# Ownership, views, limits, and diagnostics

## Parse modes

| Mode | Owns source/state | Use when |
| --- | --- | --- |
| `Parse(OwnedTextBuffer)` | Returned document owns source and arena | Normal self-contained document |
| `ParseBorrowed(BorrowedTextView, ParseScratch&)` | Input and document state are borrowed | Caller already owns stable input and wants explicit scratch |
| `ParseInSitu(MutableTextBuffer)` | Document owns mutable source; parser may rewrite it | Decoding/normalization in the owned buffer is acceptable |
| `XML::ParseSyntax` | Owns original bytes and syntax tokens | Exact formatter/editor round trip matters |

`ParseScratch` is working storage; borrowed parsing does not make external input
owned. Keep the input, document, and every view alive in the order required by
the selected mode.

## View lifetime

Moving an owning document does not relocate its shared backing state, so views
remain tied to that state. Destroying/replacing the document invalidates them.
Event parser values are even shorter lived: they are callback-scoped and must
be copied during the callback.

## Resource limits

`ParseLimits` bounds input bytes, nesting depth, nodes, object members/XML
attributes, decoded bytes, and retained memory. Supply boundary-specific limits
for untrusted input; defaults are not a universal security policy.

Limits are checked with overflow-safe accounting. A limit failure is a parse
diagnostic, not permission to retry with all limits disabled.

## Diagnostics

`ParseDiagnostic`/`ParseError` contains:

- a `ParseErrorCode`;
- byte offset plus line/column location;
- primary `SourceSpan`;
- optional related span for relationships such as a duplicate’s first site;
- descriptive text where provided.

Persist the diagnostic or source coordinates, not a view into temporary parser
callback storage.

