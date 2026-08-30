# NGIN.Log Quick Start

This guide shows the shortest practical path to using `NGIN.Log`.

The intended model is:

- application code builds message text
- `NGIN.Log` captures records, metadata, context, and attributes
- output formatters render records for sinks

## 1. Console Logging

Start with one logger and one console sink:

```cpp
#include <NGIN/Log/Log.hpp>

int main()
{
    using AppLogger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    AppLogger::SinkSet sinks {};
    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());

    AppLogger logger("App", NGIN::Log::LogLevel::Info, std::move(sinks));
    logger.Info("application started");
    logger.Warn("slow request");
}
```

Use direct-message logging when the text already exists or is cheap to build.

## 2. Structured Attributes

Operational data usually belongs in attributes, not buried in the message:

```cpp
logger.Info([&](NGIN::Log::RecordBuilder& rec) {
    rec.Message("request finished");
    rec.Attr("status", 200);
    rec.Attr("bytes", 1536);
    rec.Attr("latency_ns", std::chrono::microseconds(275));
});
```

Prefer this style for:

- IDs
- sizes and counts
- durations
- booleans and status values
- anything downstream tooling may want to filter or aggregate

## 3. Deferred Work

Builder/lambda logging is the lazy-work path.

The callback only runs if compile-time and runtime filtering both allow the record:

```cpp
#include <format>

logger.Debug([&](NGIN::Log::RecordBuilder& rec) {
    rec.Message(std::format("cache miss key={} shard={}", key, shard));
    rec.Attr("key", key);
    rec.Attr("shard", shard);
});
```

Use this style when:

- message construction is expensive
- you need both text and structured attributes
- the call site is performance-sensitive

## 4. Scoped Context

Thread-local scoped context is useful for request metadata:

```cpp
auto scope = NGIN::Log::PushLogContext({
    {"request_id", "req-42"},
    {"tenant", "alpha"},
});

logger.Info("request accepted");
```

Context caveat:

- context is thread-local only
- it does not automatically cross thread pools, executors, or coroutine resumption on another thread

## 5. Production File Logging

For production ingestion, a common setup is rotating JSON output behind an async sink:

```cpp
auto fileSink = NGIN::Memory::MakeScoped<NGIN::Log::RotatingFileSink>(
    NGIN::Log::RotatingFileSinkOptions {
        .path = "app.log",
        .append = true,
        .maxFileBytes = 16 * 1024 * 1024,
        .maxFiles = 5,
        .formatter = NGIN::Log::MakeRecordFormatter<NGIN::Log::JsonRecordFormatter>(),
    });

using AsyncFileSink = NGIN::Log::AsyncSink<NGIN::Log::RotatingFileSink>;

auto asyncSink = NGIN::Log::MakeSink<AsyncFileSink>(
    std::move(fileSink),
    NGIN::Log::AsyncSinkOptions {
        .overflowPolicy = NGIN::Log::AsyncOverflowPolicy::Block,
    });
```

This keeps:

- rotation in the file sink
- JSON rendering in the record formatter
- backpressure behavior in the async wrapper

## 6. Choosing Between The Two Logging Styles

Use direct-message logging when:

- the message is already built
- the log is simple and low-friction matters most

Use builder/lambda logging when:

- the message should only be built if enabled
- you need structured attributes
- the call site is hot or moderately hot

## Read Next

- [Production Guide](./Production.md)
- [API Guide](./API.md)
- [Sinks Guide](./Sinks.md)
- [Performance Guide](./Performance.md)
