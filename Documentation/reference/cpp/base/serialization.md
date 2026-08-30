---
title: NGIN.Serialization API reference
description: Symbol index for source ownership, limits, JSON/XML documents, parsers, views, events, builders, and writers.
---

# NGIN.Serialization API reference

**Umbrella:** `<NGIN/Serialization.hpp>`  
**Target:** `NGIN::Base::Serialization`  
**Namespaces:** `NGIN::Serialization`, `::JSON`, `::XML`

For first use, start with [Learn NGIN.Serialization](../../../libraries/base/serialization.md).

## Core

| Symbols | Reference |
| --- | --- |
| `OwnedTextBuffer`, `BorrowedTextView`, `MutableTextBuffer` | [Parsing core](./serialization/parsing-core.md) |
| `ParseLimits`, `ParseScratch`, `ParseResources`, `ParseError` | [Parsing core](./serialization/parsing-core.md) |
| `SourceId`, `SourceSpan`, `SourceLocation`, `SourceMap` | [Parsing core](./serialization/parsing-core.md#source-information) |
| `TextSink`, `MakeTextSink` | [Builders and writers](./serialization/writers.md#textsink) |

## JSON

| Symbols | Reference |
| --- | --- |
| `Parser`, `Parse`, `ParseBorrowed`, `ParseInSitu`, `ParseOptions` | [JSON](./serialization/json.md#parsing) |
| `Document`, `BorrowedDocument`, `ValueView`, `ArrayView`, `ObjectView`, `MemberView` | [JSON](./serialization/json.md#documents-and-views) |
| `EventParser`, `IncrementalEventParser`, `Event`, `EventAction` | [Events](./serialization/events.md#json-events) |
| `Builder`, `Writer`, `StreamWriter` | [Builders and writers](./serialization/writers.md) |

## XML

| Symbols | Reference |
| --- | --- |
| `Parser`, parse functions, `ParseOptions` | [XML](./serialization/xml.md#parsing) |
| `Document`, `BorrowedDocument`, `NodeView`, `ElementView`, ranges | [XML](./serialization/xml.md#semantic-documents) |
| `SyntaxDocument`, `SyntaxToken`, `SyntaxKind` | [XML](./serialization/xml.md#lossless-syntax) |
| Event parsers | [Events](./serialization/events.md#xml-events) |
| `Builder`, `Writer`, `StreamWriter` | [Builders and writers](./serialization/writers.md) |

All views borrow document or callback state. Limits and ownership mode are
part of the public security/lifetime contract, not parser implementation
details.

