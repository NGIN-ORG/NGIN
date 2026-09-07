---
title: Event parsing and stream writing
description: Process JSON and XML through callbacks/chunks and author bounded output without retaining a document.
---

# Event parsing and stream writing

## Contiguous events

`JSON::EventParser::ParseContiguous` and
`XML::EventParser::ParseContiguous` validate one complete input and invoke a
callback for structural/value events. `EventAction` lets the callback continue
or stop according to the format contract.

Event string/number/name views are callback-scoped. Copy immediately if they
must survive the return.

## Incremental input

Incremental event parsers consume `Feed(chunk)` input and deliver callbacks as
complete tokens become available. A feed returns `EventProduced` when it invokes
callbacks, otherwise `NeedMoreInput`. Each result counts only callbacks from that
call. `Finish()` marks end of input and validates completion; repeated successful
finishes are idempotent.

A later error does not retract earlier callbacks. Stage application changes until
`Finish()` succeeds if they must be atomic. Failed diagnostics remain stable until
`Reset()`. Handler exceptions propagate; reset before reuse after an exception.

Retained state consists of unfinished tokens, decoding scratch, and open
containers (including their JSON keys). Complete input is released. One very large
token can still need large storage. `BufferedBytes()` reports pending bytes;
`MemoryCommitted()` reports retained dynamic parser and scratch capacity.
Input and structural limits, and source offsets, span the whole stream.

JSON `KeepLast` buffers the document until `Finish()`, because later keys can
replace earlier values. Other JSON policies and XML deliver during `Feed()`.
`Reset()` starts another document while retaining reusable capacity.

## Text sinks

`TextSink` is a borrowed state pointer plus a `noexcept` write callback that
returns Boolean success. `MakeTextSink(std::string&)` adapts a string. Custom
sinks must remain alive for the writer and must report failure rather than
throwing through the callback boundary.

## Stream writer state

JSON writers track object/array/key/value order. XML writers track open
elements, attributes, and content state. Both enforce configured output and
depth limits. `Finish` is the only proof that nesting is complete and every
deferred sink operation succeeded.

