# NGIN.Log Production Guide

This guide covers practical sink choices, formatter choices, async policies, and operational caveats.

## Recommended Starting Topologies

### Development

Use:

- `ConsoleSink`
- `TextRecordFormatter`
- `includeSource = true`

Why:

- human-readable output matters more than ingestion shape
- source metadata is useful while iterating locally

### Service Or Backend Process

Use:

- `RotatingFileSink`
- `JsonRecordFormatter`
- `AsyncSink<RotatingFileSink>`

Why:

- JSON is easier for ingestion systems to parse reliably
- rotation keeps file growth bounded
- async delivery reduces caller-facing output latency

### Small Utility Or CLI

Use:

- `ConsoleSink`
- optionally `FileSink` for audit output
- no async wrapper unless output latency is actually a problem

Why:

- the simplest topology is usually the best one

## Formatter Selection

Choose `TextRecordFormatter` when:

- humans are the main reader
- logs are read in terminals or local files

Choose `JsonRecordFormatter` when:

- logs go to ingestion pipelines
- attributes are important to preserve exactly
- machine parsing matters more than readability

Choose `LogFmtRecordFormatter` when:

- you want text output with more structure than plain human-readable logs
- the downstream system already expects logfmt

## Telemetry Hints

Per-record attributes may now carry advisory `LogAttributeKind` hints:

- `Default`
- `Tag`
- `Context`
- `Extra`

Current behavior:

- built-in formatters ignore these hints
- future custom sinks may use them for backend-specific mapping
- scoped context still merges into the record as `Default` attributes in this pass

## Async Overflow Policies

`AsyncSink` supports multiple overflow behaviors. Treat this as an operational policy choice, not a cosmetic option.

### `DropNewest`

Best for:

- low-latency paths where log loss is acceptable
- debug-heavy workloads where blocking callers would be worse than dropping records

Tradeoff:

- records can be lost under pressure

### `Block`

Best for:

- systems where losing records is unacceptable
- moderate-throughput services where bounded backpressure is acceptable

Tradeoff:

- producers can stall when the queue is full

### `BlockForTimeout`

Best for:

- systems that want a bounded wait before dropping
- services where temporary bursts should block briefly, but not indefinitely

Tradeoff:

- behavior becomes time-sensitive and should be documented clearly

### `SyncFallback`

Best for:

- systems that strongly prefer eventual delivery over queue drops
- cases where caller threads can occasionally absorb sink cost

Tradeoff:

- caller latency becomes unpredictable under sustained pressure

## File Sink Guidance

Use `FileSink` when:

- you need one file
- external tooling already manages rotation
- operational simplicity matters more than built-in rotation features

Use `RotatingFileSink` when:

- the process owns its own log file lifecycle
- size- or day-based rotation is expected
- retention should stay bounded without external coordination

## Context Guidance

Scoped context is useful for:

- request IDs
- tenant or shard IDs
- trace-like metadata passed through a single thread

Be careful when:

- work moves across thread pools
- coroutines resume on a different worker thread
- you assume context propagation behaves like task-local storage

Current rule:

- `PushLogContext(...)` is thread-local only

## Source Metadata Guidance

Source metadata is built into direct-message and builder logging APIs.

Use it as:

- debugging and diagnostics metadata
- optional text output in development
- structured source data in JSON or logfmt output

Do not move source into normal user attributes unless you have a very specific downstream schema reason to duplicate it.

## Registry Guidance

Use `LoggerRegistry` when:

- logger names are dynamic or shared across subsystems
- runtime sink swaps or min-level changes should update existing loggers
- you want longest-prefix configuration like `net.http` or `db.replica`

Avoid using the registry as a substitute for application configuration discipline. Prefer a small set of documented logger name prefixes.

## Practical Defaults

If you do not want to think too hard on day one, use:

- runtime default min level: `Info`
- development sink: `ConsoleSink`
- production sink: `AsyncSink<RotatingFileSink>`
- production formatter: `JsonRecordFormatter`
- async overflow policy: `Block`

Then adjust only when workload evidence says those defaults are wrong.

## Read Next

- [Quick Start](./QuickStart.md)
- [Sinks Guide](./Sinks.md)
- [Architecture](./Architecture.md)
- [Performance Guide](./Performance.md)
