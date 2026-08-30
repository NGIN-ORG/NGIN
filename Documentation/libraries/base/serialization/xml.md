---
title: XML semantic and lossless parsing
description: Parse secure semantic XML or preserve exact syntax trivia for editor and formatter workflows.
---

# XML semantic and lossless parsing

## Semantic parser

The semantic profile requires one document element, matching tags, unique
quoted attributes, valid characters/UTF-8, and valid predefined/numeric entity
references. It normalizes line endings.

`DOCTYPE` is rejected by default. The opt-in allow-without-external-entities
policy still rejects `SYSTEM`/`PUBLIC` identifiers and does not expand custom
entities. The default model preserves qualified-name spelling but does not
resolve namespace prefixes to URI/local-name pairs.

Use `ElementView::Attribute`, `Children(name)`, `FirstChild`, and `FirstText`.
Node/attribute/child ranges borrow document state.

## Lossless syntax

`XML::ParseSyntax(OwnedTextBuffer)` returns `SyntaxDocument`, which retains
tokens and original bytes for comments, whitespace, quote style, CDATA,
declarations, processing instructions, and line endings. Choose this before
semantic parsing if byte-preserving rewrite is a requirement; discarded trivia
cannot be reconstructed later.

## Builder

`XML::Builder` constructs semantic documents. A node can have one parent.
Duplicate child handles, reattachment, and finishing an already attached node
as root are rejected with `BuildDiagnostic`.

## Writers

`XML::Writer` serializes document/syntax views according to options.
`XML::StreamWriter` validates element nesting, attribute position, text, output
limits, and sink behavior. Check `Finish` and do not reuse until `Reset`.

