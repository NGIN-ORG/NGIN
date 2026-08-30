---
title: NGIN.Log
description: Standalone C++23 structured logging with explicit records, formatting, sinks, context, and bounded asynchronous delivery.
---

# NGIN.Log

`NGIN.Log` separates record capture, formatting, and transport. Applications
can use it independently or through NGIN.Core integration.

## Start here

1. Create a console logger in the [quick start](./log/quick-start.md).
2. Learn [records and formatting](./log/records-formatting.md).
3. Choose [sinks and production](./log/sinks-production.md) behavior explicitly.
4. Use the [NGIN.Log C++ reference](../reference/cpp/log/index.md) for record, formatter,
   registry, sink, overflow, and error contracts.

## Capabilities

- macro-free logger APIs;
- compile-time and runtime level filtering;
- direct messages and deferred record builders;
- typed structured attributes and scoped context;
- text, JSON, and logfmt formatting;
- console, file, rotating-file, and bounded async sinks;
- hierarchical runtime logger configuration.

Message formatting remains application code. The library records structured
facts and sends them through the configured pipeline.

Async delivery is a policy decision: choose whether a full queue drops,
blocks, waits with a timeout, or falls back to synchronous writing. Observe
drop and sink-error counters outside the logging pipeline.

## Detailed guides

The [NGIN.Log guide library](./log/guides/index.md) contains complete setup,
production configuration, sink, performance, architecture, and API guides.
