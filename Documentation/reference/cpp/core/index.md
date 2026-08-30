---
title: NGIN.Core C++ API
description: Public application host, module, service, event, task, configuration, and plugin symbols.
---

# NGIN.Core C++ API

**Header:** `<NGIN/Core/Core.hpp>`  
**Namespace:** `NGIN::Core`  
**Target:** `NGIN::Core`  
**Source:** [Packages/NGIN.Core/include/NGIN/Core](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.Core/include/NGIN/Core)

## Application hosting

| Symbol | Header | Role |
| --- | --- | --- |
| `ApplicationBuilder` | `Application.hpp` | Configures packages, modules, services, plugins, and host policy |
| `CreateApplicationBuilder` | `Application.hpp` | Creates the supported builder entry point |
| `IApplicationHost` | `Application.hpp` | Starts, runs/ticks, stops, and shuts down an application |
| `IHostRunLoop` | `Application.hpp` | Supplies an application-owned run loop |
| `KernelHostConfig` | `HostConfig.hpp` | Low-level host and scheduler policy |
| `StartupReport` | `Kernel.hpp` | Startup state, warnings, and module diagnostics |

## Modules and plugins

`IModule`, `ModuleContext`, `ModuleDescriptor`, `DependencyDescriptor`,
`ModuleCapability`, `IModuleCatalog`, `StaticModuleCatalog`, `IPluginCatalog`,
`FilesystemPluginCatalog`, and `IPluginBinaryLoader` define discovery,
dependency resolution, lifecycle, and trusted dynamic loading.

## Runtime services

| Area | Central symbols |
| --- | --- |
| Services | `IServiceRegistry`, `IServiceProvider`, `ServiceRegistry`, `ServiceLifetime`, `ServiceKey` |
| Events | `IEventBus`, `EventBus`, `EventQueue`, `EventScope`, `EventSubscriptionToken` |
| Tasks | `ITaskRuntime`, `TaskRuntime`, `TaskLane` |
| Config | `IConfigStore`, `ConfigStore`, `ConfigLayer`, `ConfigSnapshot` |
| Errors | `CoreResult<T>`, `KernelError`, `KernelErrorCode` |

Host and registry objects own the runtime graph. References obtained from a
scope do not survive that scope. Stop and shutdown are cooperative, and dynamic
plugins are trusted native code requiring compatible ABI and build settings.

See the [Core API guide](../../../api/core.md) for lifecycle examples and the
meaning of each service family.

