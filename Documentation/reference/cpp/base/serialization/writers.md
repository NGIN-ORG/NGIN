---
title: Serialization builders and writers
description: Code reference for JSON/XML builders, document writers, stream writers, diagnostics, and TextSink.
---

# Serialization builders and writers

## Builders

`JSON::Builder` and `XML::Builder` own mutable construction state and finish an
immutable document. Their format-specific `BuildErrorCode` and
`BuildDiagnostic` report invalid handles/state, resource limits, and structural
problems. XML additionally enforces one-parent node ownership.

## Document writers

`JSON::Writer` and `XML::Writer` serialize document/view input with
format-specific `WriteOptions`. `WriteDiagnostic`/`WriteErrorCode` report
invalid structure/options, output/resource limits, and sink failures.

## Stream writers

`JSON::StreamWriter` provides begin/end object/array, key, and scalar methods.
`XML::StreamWriter` provides declaration/element/attribute/text/comment and
closing operations according to its state machine. Both require `Finish` to
validate complete nesting and deferred failure; `Reset` retains capacity for
reuse.

## `TextSink`

```cpp
struct TextSink {
    using WriteFunction = bool (*)(void*, std::string_view) noexcept;
    void* state;
    WriteFunction write;
};
```

The sink borrows `state`; it must outlive the writer operation. `false` reports
output failure. `MakeTextSink` adapts supported storage such as `std::string`.
Custom callbacks must not throw across the `noexcept` function boundary.

