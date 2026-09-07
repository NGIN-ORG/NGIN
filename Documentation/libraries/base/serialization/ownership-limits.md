---
title: Serialization ownership, views, limits, and diagnostics
description: Parse strings into self-contained documents and understand view lifetimes, limits, and diagnostics.
---

# Ownership, views, limits, and diagnostics

## Parse documents

Use `JSON::Parse(text)` or `XML::Parse(text)` for ordinary parsing. The
`std::string_view` argument accepts literals, `std::string`, and views. Input
is copied into the returned document and is needed only during the call.
`XML::ParseSyntax(text)` provides the same ownership guarantee for lossless XML.

Every document parse owns its source and parsed state. Use `XML::ParseSyntax`
when exact source bytes and syntax tokens are needed for editor/formatter work.
Set `ParseOptions::source` to identify source spans without wrapping the input.

```cpp
auto document = NGIN::Serialization::JSON::Parse(
    text, {.source = NGIN::Serialization::SourceId {7}});
```

`ParseScratch` is reusable workspace for event parsing. Event values are valid
only during the callback and must be copied if retained.

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

