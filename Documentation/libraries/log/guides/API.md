# NGIN.Log API Guide

This guide describes the current public API surface in `include/NGIN/Log`.

## Header Entry Points

- `#include <NGIN/Log/Log.hpp>` for the main API

## Core Types

### `LogLevel`

`enum class LogLevel : NGIN::UInt8`

Levels:
- `Trace`
- `Debug`
- `Info`
- `Warn`
- `Error`
- `Fatal`
- `Off`

Utility:
- `ToString(LogLevel) -> std::string_view`

### `LogAttribute` and `LogRecordView`

Attribute values on the transport path remain:
- `NGIN::Int64`
- `NGIN::UInt64`
- `double`
- `bool`
- `std::string_view`

Attribute intent hints:
- `enum class LogAttributeKind : NGIN::UInt8`
- values: `Default`, `Tag`, `Context`, `Extra`

`LogAttribute` fields:
- `key`
- `value`
- `kind`

`LogRecordView` fields:
- `timestampEpochNanoseconds`
- `level`
- `loggerName`
- `message`
- `source`
- `threadIdHash`
- `attributes`
- `truncatedAttributeCount`
- `truncatedBytes`

## `RecordBuilder`

Stack-only bounded builder:
- `Message(std::string_view)`
- `Attr(std::string_view key, TValue&& value)`
- `Attr(std::string_view key, TValue&& value, LogAttributeKind kind)`

Normalized attribute inputs now include:
- signed and unsigned integrals
- floating-point values
- `bool`
- enums
- `std::string_view`, `const char*`, `std::string`
- `std::error_code`
- `std::chrono::duration`
- pointers

Bounded behavior:
- message text capped by `Config::MaxMessageBytes`
- attributes capped by `Config::MaxAttributes`
- attribute string text pooled in bounded inline storage

Attribute-kind behavior:
- `Attr(key, value)` defaults to `LogAttributeKind::Default`
- explicit kinds are advisory sink hints only
- built-in formatters ignore `kind` in this implementation

## Scoped Context

- `using ContextValue = std::variant<...>`
- `struct ContextAttribute`
- `ScopedLogContext PushLogContext(std::initializer_list<ContextAttribute>)`

Context is thread-local. Merge order is:
1. outer scopes
2. inner scopes
3. per-record attributes

Later duplicate keys win.

Current classification rule:
- scoped context contributes merged attributes with `LogAttributeKind::Default`
- a later per-record attribute replaces both the previous value and previous kind

## Formatter API

### `IRecordFormatter`

Virtual formatter contract:
- `Format(const LogRecordView&, std::string& output) noexcept`

Helpers:
- `MakeRecordFormatter<TFormatter>(...) -> RecordFormatterPtr`

Built-in formatters:
- `TextRecordFormatter`
- `JsonRecordFormatter`
- `LogFmtRecordFormatter`

Timestamp rendering styles:
- `EpochNanoseconds`
- `EpochMilliseconds`
- `Iso8601Utc`
- `Iso8601Local`

## `Logger<CompileTimeMin>`

Main logging type.

Key API:
- `SetRuntimeMin(LogLevel)`
- `GetRuntimeMin()`
- `SetSinks(SinkSet)`
- `GetSinksSnapshot()`
- `Flush()`
- `GetSinkErrorCount()`

Logging styles:
- `Log<Level>(fn, source = current())`
- direct message overloads: `Trace/Debug/Info/Warn/Error/Fatal(std::string_view, source = current())`
- builder overloads: `Trace/Debug/Info/Warn/Error/Fatal(fn, source = current())`

Behavior rule:
- builder callbacks execute only if the record passes compile-time and runtime filtering

Source rule:
- direct-message and builder APIs capture `std::source_location::current()` by default
- source is built-in record metadata, not a user-managed attribute

## Registry API

### `BasicLoggerRegistry<CompileTimeMin>`

Main registry type. Backward-compatible alias:
- `using LoggerRegistry = BasicLoggerRegistry<LogLevel::Trace>;`

Supporting types:
- `LoggerConfig`
- `LoggerRule`

Key API:
- `GetOrCreate(name, runtimeMin)`
- `Get(name)`
- `SetDefaultRuntimeMin(level)`
- `SetDefaultSinks(sinks)`
- `GetDefaultRuntimeMin()`
- `GetDefaultSinks()`
- `UpsertRule(rule)`
- `RemoveRule(prefix)`
- `GetEffectiveConfig(name)`
- `SetLoggerRuntimeMin(name, level)`
- `ReplaceLoggerSinks(name, sinks)`
- `GetLoggerNames()`

Rule matching uses the longest dot-delimited prefix.

## Sinks

Core sink contract:
- `Write(const LogRecordView&) noexcept`
- `Flush() noexcept`

Built-in sinks:
- `NullSink`
- `ConsoleSink`
- `FileSink`
- `RotatingFileSink`
- `AsyncSink<TSink, QueueCapacity, BatchSize>`

### `AsyncSink`

Options:
- `overflowPolicy`
- `blockTimeout`
- `emitDropReports`
- `notifyOnError`

Overflow policies:
- `DropNewest`
- `Block`
- `BlockForTimeout`
- `SyncFallback`

Stats:
- `GetStats()`
- `GetDroppedCount()`
- `GetErrorCount()`
- `GetEnqueuedCount()`

## API Contracts And Caveats

- Public logging APIs are `noexcept`
- sink exceptions are swallowed and counted
- re-entrant dispatch is guarded by a thread-local recursion check
- context propagation is thread-local only
- direct-message calls perform any consumer-owned message construction before entering the logger
