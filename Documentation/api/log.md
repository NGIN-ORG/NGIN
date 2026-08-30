---
title: NGIN.Log API
description: Log levels, records, builders, scoped context, formatters, loggers, registries, sinks, async delivery, and production contracts.
---

# NGIN.Log API

**Include:** `<NGIN/Log/Log.hpp>`  
**Package:** `NGIN.Log`  
**Namespace:** `NGIN::Log`

## Create a logger

```cpp
using AppLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

AppLogger::SinkSet sinks;
sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());

AppLogger logger("App", NGIN::Log::LogLevel::Info, std::move(sinks));
logger.Info("application started");
```

`LogLevel` values are `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Fatal`, and
`Off`. The logger's template level removes lower-level calls at compile time;
the runtime minimum filters the remainder.

## Structured records

```cpp
logger.Info([&](NGIN::Log::RecordBuilder& record) {
    record.Message("request finished");
    record.Attr("status", 200);
    record.Attr("bytes", NGIN::UInt64 {1536});
    record.Attr("latency", std::chrono::microseconds {275});
});
```

The builder callback executes only when the record passes both compile-time and
runtime filtering. Use it for expensive messages and structured fields.

`RecordBuilder` accepts signed/unsigned integers, floating point, bool, enums,
strings, `std::error_code`, durations, and pointers. It is bounded by
`Config::MaxMessageBytes`, `Config::MaxAttributes`, and inline attribute-string
storage. Truncation counts and bytes are carried in `LogRecordView`.

`LogAttributeKind` (`Default`, `Tag`, `Context`, `Extra`) is advisory. Built-in
formatters currently ignore the kind.

## Record view

`LogRecordView` provides timestamp nanoseconds, level, logger name, message,
source location, thread hash, attributes, truncated-attribute count, and
truncated bytes. A view is valid only during sink dispatch. Copy fields that a
sink queues beyond `Write`.

## Scoped context

```cpp
auto context = NGIN::Log::PushLogContext({
    {"request_id", "req-42"},
    {"tenant", "alpha"},
});
```

Context is thread-local and does not automatically follow work across threads
or coroutine migration. Merge order is outer scope, inner scope, then record
attributes; later duplicate keys replace earlier value and kind.

## Formatters

`IRecordFormatter::Format(const LogRecordView&, std::string&) noexcept` is the
custom formatter contract. Construct one with
`MakeRecordFormatter<TFormatter>(...)`.

Built-ins are `TextRecordFormatter`, `JsonRecordFormatter`, and
`LogFmtRecordFormatter`. Timestamp styles are epoch nanoseconds, epoch
milliseconds, ISO-8601 UTC, and ISO-8601 local.

## `Logger<CompileTimeMin>`

Main operations:

- `SetRuntimeMin` / `GetRuntimeMin`
- `SetSinks` / `GetSinksSnapshot`
- `Flush`
- `GetSinkErrorCount`
- `Log<Level>(builder)`
- `Trace`, `Debug`, `Info`, `Warn`, `Error`, and `Fatal` direct and builder forms

Direct and builder calls capture `std::source_location::current()` by default.
Direct-message text is already constructed before the logger can filter it;
use a builder for expensive construction.

## Registry

`BasicLoggerRegistry<CompileTimeMin>` owns named loggers and hierarchical
configuration. `LoggerRegistry` aliases the trace-capable specialization.

Important operations are `GetOrCreate`, `Get`, default level/sinks,
`UpsertRule`, `RemoveRule`, `GetEffectiveConfig`, per-logger level/sink changes,
and `GetLoggerNames`. Rules use the longest dot-delimited prefix, so `net.http`
is more specific than `net`.

## Sinks

The sink contract is:

```cpp
void Write(const LogRecordView&) noexcept;
void Flush() noexcept;
```

Built-ins are `NullSink`, `ConsoleSink`, `FileSink`, `RotatingFileSink`, and
`AsyncSink<TSink, QueueCapacity, BatchSize>`.

`AsyncSink` overflow policies:

| Policy | Queue-full behavior | Tradeoff |
| --- | --- | --- |
| `DropNewest` | Discard the incoming record | Bounded caller latency; data loss |
| `Block` | Wait for capacity | Backpressure can stall producers |
| `BlockForTimeout` | Wait, then drop | Bounded stall and possible loss |
| `SyncFallback` | Write on caller | Delivery preferred; latency spikes |

Inspect `GetStats`, `GetDroppedCount`, `GetErrorCount`, and
`GetEnqueuedCount`. Flush and destroy the async sink during orderly shutdown.

## Error and re-entrancy contract

Public logging APIs are `noexcept`. Sink exceptions are swallowed and counted.
Re-entrant dispatch is guarded per thread. Logging must never be the only place
an operational failure is reported—observe sink error/drop counters through a
separate health path.

For a service, a practical starting topology is JSON → rotating file → async
sink with an overflow policy chosen explicitly. For a CLI, a synchronous
console sink is usually easier and safer.

**Source:** [`NGIN.Log` public headers](https://github.com/NGIN-ORG/NGIN/tree/main/Dependencies/NGIN/NGIN.Log/include/NGIN/Log)

