# NGIN.Log Performance Guide

This guide explains what the benchmark cases measure and how to interpret them.

## What The Library Is Optimizing For

`NGIN.Log` is designed for:

- cheap compile-time filtered call sites
- bounded record construction
- predictable behavior under stress
- sync and async delivery paths that do not surprise you operationally

It is not trying to make every message-formatting style equally fast. Message construction is application-owned by design.

## Benchmark Categories

Typical benchmark cases should be read like this:

### Compile-Time Filtered

Example:

- `disabled-debug-compile-filter`

What it measures:

- the cost of a disabled call site that compiles out below `CompileTimeMin`

Good signal:

- this should be near noise-floor territory

### Direct Message

Example:

- `enabled-info-direct-message-null-sink`

What it measures:

- direct-message logging plus whatever work the caller performs to build the message text before entering the logger

Important:

- if the call site uses `std::format(...)`, the result includes formatting cost

### Builder Deferred Message

Example:

- `enabled-builder-deferred-message-null-sink`

What it measures:

- enabled builder/lambda logging where the callback builds the message

Important:

- this shows deferred work behavior only when the level is disabled
- when the level is enabled, any `std::format(...)` inside the callback still costs what it costs

### Structured Builder

Example:

- `enabled-structured-builder-null-sink`

What it measures:

- the library's cleaner hot path: bounded builder use, structured attributes, and minimal transport overhead

This is often the most representative signal for core logger overhead.

### Contextual

Example:

- `enabled-contextual-null-sink`

What it measures:

- scoped context setup plus merge cost in addition to the normal logging path

### Async File Or JSON

Example:

- `enabled-async-rotating-json`

What it measures:

- a more production-like topology including record formatting, queueing, and file output behavior

Important:

- do not compare this directly to `NullSink` numbers as if they were the same workload

## How To Read The Numbers

When a benchmark looks slower than expected, first ask:

1. Is the number mostly logger overhead, or mostly caller-owned message construction?
2. Is the sink synthetic like `NullSink`, or real like file + JSON + async?
3. Is context enabled?
4. Are attributes being normalized?

The library is doing well when:

- compile-time filtered calls are effectively free
- structured builder logging to `NullSink` stays small and stable
- async file logging scales predictably without pathological stalls or allocation spikes after warmup

## Running The Benchmarks

From the repo root:

```bash
cmake -S Dependencies/NGIN/NGIN.Log -B build/ngin-log-ci \
  -DNGIN_LOG_BUILD_TESTS=ON \
  -DNGIN_LOG_BUILD_EXAMPLES=ON \
  -DNGIN_LOG_BUILD_BENCHMARKS=ON

cmake --build build/ngin-log-ci --target NGINLogBenchmarks
./build/ngin-log-ci/benchmarks/NGINLogBenchmarks
```

## Recommended Extra Cases

If you want a cleaner picture of overhead breakdown, add:

- direct prebuilt message to `NullSink`
- builder message-only to `NullSink`
- structured builder with and without context
- async sink with each overflow policy

Those cases make it easier to separate:

- caller-owned formatting cost
- core logger overhead
- attribute normalization cost
- context merge cost
- async transport cost

## What To Avoid

Avoid conclusions like:

- "direct message is slower than builder, so the logger is broken"
- "async file logging is expensive, so the queue is bad"

Those are often category errors caused by comparing fundamentally different workloads.

## Read Next

- [Quick Start](./QuickStart.md)
- [Production Guide](./Production.md)
- [Architecture](./Architecture.md)
