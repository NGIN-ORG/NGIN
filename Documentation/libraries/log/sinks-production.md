---
title: Log sinks and production
description: Choose console, file, rotation, asynchronous delivery, overflow, flush, and shutdown behavior.
---

# Sinks and production

Sinks transport formatted records. Production behavior depends as much on
queue and failure policy as on the selected destination.

## Available shapes

- console sink for interactive output;
- file sink for persistent output;
- rotating file sink for bounded local retention;
- async wrapper for bounded queued delivery.

## Async policy

A bounded queue must define what happens when producers are faster than the
sink:

| Policy | Trade-off |
| --- | --- |
| Block | Preserves records but transfers latency to the caller |
| Drop | Preserves caller latency but loses records |
| Escalate/fallback | Preserves visibility through another path at added complexity |

Choose policy by severity and workload. A debug trace and an audit event may
require different guarantees.

## Rotation

Set maximum file size, retained file count, append behavior, and format
explicitly. Rotation limits local storage; it is not a backup or centralized
retention strategy.

## Shutdown

Drain and flush owned async sinks before destroying their destination. Define a
timeout and an observable fallback for records that cannot be delivered during
process shutdown.
