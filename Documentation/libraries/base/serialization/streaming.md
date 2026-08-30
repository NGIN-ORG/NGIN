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

Incremental event parsers accept `Feed(chunk)` and complete with `Finish()`.
They retain enough input/state to validate the complete document before event
commit. `Feed` normally reports `NeedMoreInput`; `Finish` emits exactly once and
is idempotent. A failure diagnostic remains stable until `Reset`.

`Reset` starts another document while retaining capacity. It invalidates prior
transient views and parser state.

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

