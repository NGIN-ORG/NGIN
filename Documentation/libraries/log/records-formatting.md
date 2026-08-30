---
title: Log records and formatting
description: Capture messages, structured attributes, scoped context, and render them independently.
---

# Records and formatting

A record captures an event. A formatter decides how that event appears in one
sink.

```text
log call ─► record + attributes + context ─► formatter ─► sink
```

## Direct messages

Use direct logging when message text already exists or is cheap to construct:

```cpp
logger.Info("server listening");
```

## Deferred builders

Use a builder when work should occur only after filtering or when downstream
tools need typed attributes:

```cpp
logger.Debug([&](NGIN::Log::RecordBuilder& record) {
    record.Message("cache miss");
    record.Attr("key", key);
    record.Attr("shard", shard);
});
```

## Scoped context

Scoped context is useful for request or operation metadata. It is thread-local
and does not automatically cross a thread pool or coroutine resumption on
another thread. Capture and restore context explicitly at those boundaries.

## Formats

Text is readable at a terminal. JSON and logfmt are suitable for structured
ingestion. Formatting belongs to the sink pipeline, so the application record
does not need to change when transport format changes.
