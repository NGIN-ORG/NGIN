---
title: Serialization event parsers
description: Code reference for contiguous and incremental JSON/XML event parsing and callback lifetime.
---

# Serialization event parsers

## JSON events

**Header:** `<NGIN/Serialization/JSON/JsonEventParser.hpp>`

`JSON::EventKind` classifies structural and scalar events. `Event` carries the
kind plus callback-scoped value/name/source information. `EventAction` controls
continuation/rejection. `EventParser::ParseContiguous` validates one complete
input and invokes the callback.

`IncrementalEventParser` accepts `Feed` chunks, completes/emits through
`Finish`, reports `IncrementalParseResult`, and resets for another document.

## XML events

**Header:** `<NGIN/Serialization/XML/XmlEventParser.hpp>`

The XML equivalents expose XML `EventKind`, event payload, action, contiguous
parser, and incremental parser under `NGIN::Serialization::XML`.

## Incremental contract

`IncrementalParseStatus` distinguishes need-more-input, completion, and
failure. The parser validates retained complete state before committing events.
`Finish` is idempotent after completion; a failed diagnostic remains stable
until `Reset`.

Every event view is callback-scoped, including text assembled from multiple
chunks. Copy before returning if persistence is required.

