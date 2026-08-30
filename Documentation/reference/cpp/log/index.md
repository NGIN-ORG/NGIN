---
title: NGIN.Log C++ API
description: Public logger, record, context, formatter, registry, sink, and async-delivery symbols.
---

# NGIN.Log C++ API

**Header:** `<NGIN/Log/Log.hpp>`  
**Namespace:** `NGIN::Log`  
**Target:** `NGIN::Log`  
**Source:** [Dependencies/NGIN/NGIN.Log/include/NGIN/Log](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Log/include/NGIN/Log)

## Records and logging

| Symbol | Header | Role |
| --- | --- | --- |
| `Logger<CompileTimeMin>` | `Logger.hpp` | Level filtering, record dispatch, sinks, flush, error counts |
| `RecordBuilder` | `RecordBuilder.hpp` | Builds one structured record before dispatch |
| `LogRecordView` | `Types.hpp` | Borrowed record passed to a sink/formatter |
| `OwnedLogRecord` | `Types.hpp` | Owned record for storage or asynchronous delivery |
| `LogLevel` | `LogLevel.hpp` | Trace through fatal severity |
| `ScopedLogContext` | `Context.hpp` | Scope-bound structured context |

## Formatting and sinks

`IRecordFormatter` is implemented by `TextRecordFormatter`,
`JsonRecordFormatter`, and `LogFmtRecordFormatter`. `ILogSink` is implemented by
`NullSink`, `ConsoleSink`, `FileSink`, `RotatingFileSink`, and `AsyncSink`.

`AsyncSinkOptions`, `AsyncOverflowPolicy`, and `AsyncSinkStats` make queue
capacity, backpressure, loss, and fallback observable. A borrowed
`LogRecordView` is valid only during the call; asynchronous sinks must retain an
`OwnedLogRecord`.

## Registry

`BasicLoggerRegistry`, `LoggerConfig`, and `LoggerRule` provide hierarchical
runtime configuration while the logger template retains compile-time level
elision.

Sink failures are counted and must not recursively log through the same failing
pipeline. See the [Log API guide](../../../api/log.md) for record construction,
formatting, overflow behavior, and re-entrancy rules.

