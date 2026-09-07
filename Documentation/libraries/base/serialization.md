---
title: Learn NGIN.Serialization
description: Parse, inspect, build, stream, and write strict JSON and XML with explicit ownership, limits, and diagnostics.
---

# Learn NGIN.Serialization

**Include:** `<NGIN/Serialization.hpp>`  
**Target:** `NGIN::Base::Serialization`  
**Namespaces:** `NGIN::Serialization::JSON`, `NGIN::Serialization::XML`

Serialization provides format-specific JSON and XML APIs. It does not expose a
generic object archive.

## Start here

1. [Parse your first document](./serialization/first-document.md).
2. Learn [ownership, views, limits, and diagnostics](./serialization/ownership-limits.md)
   before accepting untrusted input.
3. Choose [JSON](./serialization/json.md) or [XML](./serialization/xml.md)
   semantics deliberately.
4. Use [event parsing and stream writing](./serialization/streaming.md) when a
   retained document is not the right boundary.
5. Look up exact declarations in the
   [Serialization API reference](/reference/doxygen/base/namespaceNGIN_1_1Serialization.html).

The central decision is what must own the input and parsed state:

```text
input bytes → parse mode → document state → borrowed views
                 │              │                 │
                 └─ limits      └─ owns arena     └─ never outlive state
```

## Parse text into an owning document

| Parse API | Returned document | Lifetime rule |
| --- | --- | --- |
| `JSON::Parse(std::string_view)` / `XML::Parse(...)` | Owned semantic document | Self-contained and movable |
| `XML::ParseSyntax(std::string_view)` | Lossless syntax document | Preserves authored bytes and tokens for formatter/editor round trips |

`JSON::Parse(text)` and `XML::Parse(text)` accept `std::string_view` and copy
the input into the returned document. The input can be modified or destroyed
after the call returns. Views into a document remain valid while its backing
state lives; moving a document does not relocate that state.

Set `ParseOptions::source` to attach a `SourceId` to spans and diagnostics.
All document parse results own their storage.

## JSON

```cpp
auto document = NGIN::Serialization::JSON::Parse(
    R"({"count":42})");
if (!document) {
    return Report(document.error());
}

auto root = document->Root();
auto object = root.TryObject();
if (!object) return ReportInvalidRoot();
auto value = object->Find("count");
if (!value) return ReportMissingCount();
auto count = value->TryInt64();
```

The default profile rejects comments, trailing commas, duplicate keys, invalid
UTF-8, invalid escapes, and malformed surrogate pairs. Integers stay `Int64` or
`UInt64`; they are not first converted through `double`. Non-integral numbers
must be finite.

Duplicate behavior is explicit in `JSON::ParseOptions`: `Reject`, `Preserve`,
`KeepFirst`, or `KeepLast`. Prefer checked `Try*` access and `ObjectView::Find`
for untrusted documents.

## XML

The semantic parser requires one document element, matching tags, unique
quoted attributes, valid UTF-8/characters, and valid predefined/numeric entity
references. It normalizes line endings.

`DOCTYPE` is rejected by default. The opt-in
`AllowWithoutExternalEntities` profile still rejects `SYSTEM` and `PUBLIC`
identifiers and does not expand custom entities. Namespace spelling is
preserved but the default semantic model does not resolve prefixes into URI and
local-name pairs.

Use `ElementView::Attribute`, `Children(name)`, `FirstChild`, and `FirstText`
for allocation-free reads. Semantic nodes carry source spans.

Semantic parsing preserves the original source and stores decoded text
separately. Use `ParseSyntax` when output must preserve comments, whitespace,
quote choices, and line endings exactly.

## Limits and diagnostics

`ParseLimits` bounds input bytes, depth, nodes, members/attributes, decoded
bytes, and total retained memory. Apply limits at every untrusted boundary.

`ParseDiagnostic` reports the error code, byte/line/column position, primary
source span, and sometimes a related span such as the first duplicate key.

```cpp
if (!document) {
    const auto& diagnostic = document.error();
    Show(diagnostic.code, diagnostic.line, diagnostic.column);
}
```

## Event parsers

- `JSON::EventParser::ParseContiguous` and
  `XML::EventParser::ParseContiguous` process one complete buffer.
- `JSON::IncrementalEventParser` and `XML::IncrementalEventParser` accept
  `Feed()` chunks and deliver events during `Feed()` and `Finish()`.

Incremental parsers retain unfinished tokens and open-container state. `Feed()`
returns `EventProduced` when callbacks ran, otherwise `NeedMoreInput`; `Finish()`
checks completion and becomes idempotent on success. A later error does not
retract earlier events. Stage consumer changes until successful completion when
atomic updates are required. JSON `KeepLast` buffers the document until `Finish()`
to resolve later duplicate keys. `Reset()` starts another document while retaining
capacity; failed diagnostics remain stable until reset.

Event values are callback-scoped, including values assembled from several
chunks. Copy any value that must survive the callback.

## Build documents

`JSON::Builder` and `XML::Builder` produce immutable semantic documents. An XML
node can have one parent; the builder rejects duplicate child handles,
reattachment, and finishing an already attached node as root.

## Write documents and streams

`JSON::Writer` and `XML::Writer` serialize document views. For output authored
directly by application code, use a stream writer:

```cpp
std::string output;
NGIN::Serialization::JSON::StreamWriter writer {
    NGIN::Serialization::MakeTextSink(output)};

writer.BeginObject();
writer.Key("sequence");
writer.UInt64(sequence);
writer.EndObject();
auto finished = writer.Finish();
```

Stream writers validate nesting and enforce output/depth limits. `Reset()`
retains stack capacity. Always check `Finish()`; it detects incomplete
containers and deferred output failures.

## Common failures

| Symptom | Cause | Fix |
| --- | --- | --- |
| Dangling `ValueView` | Borrowed input or document was destroyed | Keep both alive or use owning parse |
| Parser accepts too much data | Default/custom limits are not appropriate for the boundary | Supply explicit `ParseLimits` |
| Formatter changes user XML | Semantic parse discarded syntax trivia | Use `ParseSyntax` and write the `SyntaxDocument` |
| Saved event text is corrupted | Callback-scoped view was retained | Copy it during the callback |

**Source:** [`NGIN/Serialization`](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Serialization)
