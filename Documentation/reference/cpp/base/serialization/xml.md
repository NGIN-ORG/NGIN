---
title: XML API
description: Code reference for semantic and lossless XML parsing, documents, views, ranges, and profiles.
---

# XML API

**Header:** `<NGIN/Serialization/XML.hpp>`  
**Namespace:** `NGIN::Serialization::XML`

## Parsing

Entry points are `Parse(OwnedTextBuffer, ...)`, `ParseInSitu`,
`ParseBorrowed`, and `ParseSyntax(OwnedTextBuffer, ...)`. `Parser` provides the
same operation family. `ParseOptions` selects trivia and doctype policy.

## Semantic documents

`Document`/`BorrowedDocument` expose a root `ElementView`. `NodeView` identifies
`NodeKind`, text/name/span and conversions. `ElementView` provides attributes,
child ranges/filtering, first-child/text operations, and source information.
`AttributeView`, `AttributeRange`, `ChildRange`, and `FilteredChildRange` borrow
document state.

The semantic model validates/normalizes according to profile and does not
promise preservation of every authored byte/trivia.

## Lossless syntax

`SyntaxDocument` owns original source plus `SyntaxToken` values classified by
`SyntaxKind`. It is the reference surface for byte/trivia-preserving tooling.

## Security/profile notes

Default doctype policy rejects it. The opt-in without-external-entities mode
still rejects external identifiers and does not expand custom entities. QName
spelling is preserved; namespace URI resolution is not performed by the
default semantic model.

## Related

- [Event parsers](./events.md)
- [Builders and writers](./writers.md)
- [XML learning guide](../../../../libraries/base/serialization/xml.md)

