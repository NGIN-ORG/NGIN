---
title: Modules and plugins
description: Compose static modules and trusted dynamically loaded native plugins.
---

# Modules and plugins

A module participates in application lifecycle and service composition. A
plugin is a trusted native artifact that can register one or more modules and
carry runtime resources.

## Static modules

Register a module with the application builder when it is linked into the
product:

```cpp
builder->AddModule<Startup>("App.Startup");
```

Static modules provide the simplest deployment and ABI story. Prefer them when
the module set is known at build time.

## Dynamic plugins

A dynamic plugin carries a descriptor and exports a registrar from its native
library:

```cpp
extern "C" NGIN::Core::CoreResult<void>
NGIN_RegisterPlugin(NGIN::Core::IPluginModuleRegistry& registry);
```

The package and StagePlan must place both the library and required descriptor
or resources in the staged layout.

## Security and compatibility

Plugins execute in process with application privileges. NGIN.Core does not
currently provide sandboxing, signature verification, hot reload, or a stable
cross-compiler plugin ABI. Load only trusted artifacts built for a compatible
toolchain and runtime.

## Unloading

Unload can succeed only when plugin-owned modules, services, callbacks, events,
tasks, and values no longer escape the plugin lifetime. Prefer disabling
unload when ownership cannot be proven.
