---
title: JSON API
description: Code reference for JSON parse policies, documents, typed views, and parse entry points.
---

# JSON API

**Header:** `<NGIN/Serialization/JSON.hpp>`  
**Namespace:** `NGIN::Serialization::JSON`

## Parsing

```cpp
ParseResult<Document> Parse(OwnedTextBuffer, ParseOptions = {}, ParseLimits = {});
ParseResult<Document> ParseInSitu(MutableTextBuffer, ParseOptions = {}, ParseLimits = {});
ParseResult<BorrowedDocument> ParseBorrowed(
    BorrowedTextView, ParseScratch&, ParseOptions = {}, ParseLimits = {});
```

`Parser` exposes the same operation families as instance/static entry points.
`ParseOptions` contains `CommentPolicy`, `TrailingCommaPolicy`,
`DuplicateKeyPolicy`, and `Utf8Policy`.

## Documents and views

`Document` owns immutable parsed state and returns `Root()`. `BorrowedDocument`
retains the borrowed-mode state contract. Both are movable rather than
copyable ownership values.

`ValueView` reports `ValueKind`, validity, span, exact kind predicates, and
checked `TryBool`, `TryInt64`, `TryUInt64`, `TryDouble`, `TryString`, `TryArray`,
and `TryObject`. Preconditions apply to unchecked `As*` accessors.

`ArrayView` iterates/indexes values. `ObjectView` iterates members and finds by
name. `MemberView` exposes name, value, and source information. All borrow the
document.

## Numeric and duplicate contract

Signed and unsigned integers remain exact 64-bit values. Floating values must
be finite. Duplicate behavior follows the selected policy: reject, preserve,
keep first, or keep last.

## Related

- [Event parsers](./events.md)
- [Builders and writers](./writers.md)
- [JSON learning guide](../../../../libraries/base/serialization/json.md)

