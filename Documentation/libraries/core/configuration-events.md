---
title: Configuration and events
description: Compose layered settings and communicate through typed immediate or deferred events.
---

# Configuration and events

Configuration supplies startup and runtime settings. Events communicate typed
facts between parts of the hosted application.

## Layered configuration

Configuration sources form an ordered set. Later sources may override earlier
values according to explicit precedence.

```text
defaults ─► application file ─► environment ─► command line
```

Document the actual source order used by the application. Sensitive values
should come from an appropriate secret source rather than a committed config
file.

## Validate at the boundary

Read raw settings into a typed options object and validate it during startup.
Do not allow an invalid required setting to survive until the first code path
that happens to use it.

## Typed events

Immediate events run their handlers in the publishing flow. Deferred events
cross an execution boundary and require explicit queue, ordering, shutdown, and
error behavior.

Use events for notifications that may have multiple observers. Use a direct
service call when a caller requires one specific operation and its result.

## Ownership

Event subscriptions must not outlive their listener. Plugin and module shutdown
should remove subscriptions before destroying the state captured by handlers.
