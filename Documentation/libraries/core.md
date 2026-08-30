---
title: NGIN.Core
description: Optional application hosting with services, modules, lifecycle, configuration, events, task lanes, logging, and plugins.
---

# NGIN.Core

`NGIN.Core` is the optional application host for NGIN projects. Use it when an
application needs services, modules, configuration, lifecycle events, task
lanes, logging, or dynamic plugins.

Plain native applications do not need it.

## Start here

1. Build a minimal host in the [quick start](./core/quick-start.md).
2. Understand the [application lifecycle](./core/application-lifecycle.md).
3. Add [services](./core/services.md) and [modules](./core/modules-plugins.md)
   at explicit ownership boundaries.
4. Look up host, service, event, task, config, plugin, and error contracts in
   the [NGIN.Core C++ reference](../reference/cpp/core/index.md).

## Capabilities

| Area | Provides |
| --- | --- |
| [Application lifecycle](./core/application-lifecycle.md) | Startup, ticking, stop requests, and orderly shutdown |
| [Services and scopes](./core/services.md) | Singleton, scoped, and transient dependency injection |
| [Dependency injection](./core/dependency-injection.md) | Constructor dependencies, registration, scopes, cycles, and failures |
| [Modules and plugins](./core/modules-plugins.md) | Static modules and trusted dynamic native modules |
| [Configuration and events](./core/configuration-events.md) | Layered configuration and typed immediate or deferred events |

## Integration

NGIN.Core integrates with `NGIN.Log` and can host UI through the separate
`NGIN.UI.Hosting` package. Reflection is optional: typed constructor injection
does not require runtime reflection.

## Reference entry points

- `CreateApplicationBuilder` and `ApplicationBuilder` configure the host.
- `IApplicationHost` owns start, run/tick, stop, and shutdown.
- `IModule` and `ModuleContext` define module lifecycle and runtime access.
- `IServiceRegistry`, `IEventBus`, `ITaskRuntime`, and `IConfigStore` expose
  the main services.
- `CoreResult<T>` and `KernelError` carry structured failures.

Use the [symbol index](../reference/cpp/core/index.md) for code lookup and the
[Core API guide](../api/core.md) for cross-cutting contract explanation.

> [!WARNING]
> NGIN.Core and its plugin ABI are experimental. Dynamic plugins must be trusted
> native code built for a compatible compiler and runtime ABI.
