# NGIN.Log Sinks Guide

This guide covers sink contracts, built-in sink behavior, and operational considerations.

## Sink Contract

All sinks implement:

```cpp
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogRecordView& record) noexcept = 0;
    virtual void Flush() noexcept = 0;
};
```

Expected behavior:
- `Write` must consume record data synchronously with respect to the `LogRecordView` lifetime
- `Flush` pushes buffered data to the underlying output
- methods are `noexcept`

## Formatter Construction

Formatters are separate from transport sinks:

```cpp
auto json = NGIN::Log::MakeRecordFormatter<NGIN::Log::JsonRecordFormatter>();
```

Transport sinks default to `TextRecordFormatter` if no formatter is supplied.

## `NullSink`

Purpose:
- benchmark/control path
- disable output without changing call sites

## `ConsoleSink`

Options:
- `useStderrForErrors`
- `includeSource`
- `autoFlush`
- `formatter`

Behavior:
- formats into an internal scratch buffer
- writes to stdout/stderr under a mutex

## `FileSink`

Options:
- `append`
- `autoFlush`
- `formatter`

Behavior:
- formats into an internal scratch buffer
- writes to one file handle under a mutex
- supports `Reopen()` for manual reopen workflows

## `RotatingFileSink`

Options:
- `path`
- `append`
- `autoFlush`
- `maxFileBytes`
- `maxFiles`
- `rotateAtStartup`
- `rotateDailyLocal`
- `detectExternalRotation`
- `formatter`

Behavior:
- rotates by size and/or local-day boundary
- maintains numbered retention files
- can reopen if an external rotation is detected on supported platforms

## `AsyncSink<TSink, QueueCapacity, BatchSize>`

Construction:

```cpp
auto fileSink = NGIN::Memory::MakeScoped<NGIN::Log::RotatingFileSink>(
    NGIN::Log::RotatingFileSinkOptions {.path = "app.log"});

auto async = NGIN::Log::MakeSink<NGIN::Log::AsyncSink<NGIN::Log::RotatingFileSink>>(
    std::move(fileSink),
    NGIN::Log::AsyncSinkOptions {.overflowPolicy = NGIN::Log::AsyncOverflowPolicy::Block});
```

Behavior:
- producer `Write` copies the record into an owned inline payload
- bounded queue keeps transport allocation-free after warmup
- worker thread drains and writes to the wrapped sink
- drop reporting is optional

Overflow policies:
- `DropNewest`
- `Block`
- `BlockForTimeout`
- `SyncFallback`

Stats:
- `enqueued`
- `delivered`
- `dropped`
- `timeoutDropped`
- `fallbackWrites`
- `errors`
- `queueHighWatermark`
- `currentApproxDepth`

## Choosing Topologies

Typical patterns:
- development: `ConsoleSink`
- production text file: `FileSink`
- production structured file: `RotatingFileSink` + `JsonRecordFormatter`
- latency-sensitive file logging: `AsyncSink<RotatingFileSink>`
- multi-destination fan-out: combine sinks in one `SinkSet`
