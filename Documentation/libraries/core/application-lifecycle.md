---
title: Application lifecycle
description: Understand builder configuration, startup, ticking, stop requests, and shutdown ordering.
---

# Application lifecycle

The host owns the application lifetime. Modules and services join that lifetime
through explicit callbacks and scopes.

```text
create builder
     │
configure services, modules, configuration, lanes
     │
   Build()
     │
module startup ──► run/tick ──► stop request ──► shutdown
```

## Builder phase

Use the builder to describe the application before runtime state is created.
Registration failures should be reported during `Build()` rather than deferred
until an unrelated service lookup.

## Startup

Modules receive a context and may register or resolve the capabilities allowed
for that lifecycle phase. A failed required startup step prevents the host from
entering its run loop.

## Running and stopping

The host coordinates ticking and stop requests. Code should request shutdown
through the host rather than terminating the process from an arbitrary module.
That gives modules, task lanes, sinks, and plugins an opportunity to drain.

## Shutdown

Shutdown ordering must respect dependencies and outstanding ownership. A
service should not be destroyed while a module or deferred task still owns a
valid reference to it.

Use explicit timeouts and diagnostics for work that can delay shutdown.
