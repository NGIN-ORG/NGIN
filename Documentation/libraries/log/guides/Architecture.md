# NGIN.Log Architecture

This document describes runtime behavior and performance-relevant design decisions.

## High-Level Flow

Application call site:
1. choose a static level at the call site
2. apply compile-time filtering
3. apply runtime level filtering
4. materialize a bounded `RecordBuilder`
5. merge thread-local context with builder attributes
6. dispatch one immutable `LogRecordView` to the active sink snapshot

## Compile-Time + Runtime Filtering

For `Logger<CompileTimeMin>`:
- calls below `CompileTimeMin` compile out
- enabled calls still check the logger runtime minimum

This keeps dead-stripping predictable while preserving live operational control.

## Sink Publication Model

`Logger` publishes sinks through an atomic `std::shared_ptr<const SinkSet>` snapshot.

Implications:
- dispatch reads are lock-free at the logger level
- `SetSinks` publishes a replacement snapshot without retaining every historical generation forever
- old snapshots disappear automatically when no dispatch path still references them

## Record Materialization

`RecordBuilder` is bounded and stack-based:
- message storage: `Config::MaxMessageBytes`
- attribute count: `Config::MaxAttributes`
- attribute text pool: `Config::MaxAttrTextBytes`

Overflow is non-fatal:
- excess attributes are counted
- excess text bytes are truncated and counted

## Context Merge Model

Scoped log context is thread-local.

Merge order:
1. outer context scopes
2. inner context scopes
3. per-record attributes

If the same key appears multiple times, the later value wins in the final dispatched record.

## Formatter / Transport Split

`ConsoleSink`, `FileSink`, and `RotatingFileSink` are transport sinks.
They hold a reusable scratch buffer and delegate output rendering to an `IRecordFormatter`.

Built-in formatter styles:
- human-readable text
- JSON
- logfmt

This separation keeps destination concerns independent from wire shape concerns.

## Async Sink Behavior

`AsyncSink<TSink>`:
- owns a concrete wrapped sink
- copies `LogRecordView` into an owned inline record representation
- moves those owned records through a fixed-capacity bounded queue
- drains them on a worker thread

Producer-side overflow behavior is configurable:
- drop newest
- block
- block with timeout
- synchronous fallback delivery

Flush semantics:
- wait until queued-or-inflight work reaches zero
- emit any pending drop report if enabled
- flush the wrapped sink

## Failure Model

Public APIs are `noexcept`.

If a sink or formatter throws:
- the logger or async sink catches the exception
- increments the relevant error counter
- continues operating where possible

## Output Safety

Built-in formatters escape control characters so one logical record remains one physical line for text and logfmt output.

Timestamp rendering is formatter-controlled:
- `EpochNanoseconds`
- `EpochMilliseconds`
- `Iso8601Utc`
- `Iso8601Local`
