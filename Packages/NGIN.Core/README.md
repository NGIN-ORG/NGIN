# NGIN.Core

`NGIN.Core` is the optional application host for NGIN projects. Use it when an
application needs services, modules, configuration, lifecycle events, task
lanes, logging, or dynamic plugins. Plain native projects do not need it.

> [!WARNING]
> NGIN.Core is experimental. Its public API and plugin ABI may change before a
> stable release.

## Quick start

```cpp
#include <NGIN/Core/Core.hpp>

class Startup final : public NGIN::Core::IModule {
public:
    auto OnStart(NGIN::Core::ModuleContext& context) noexcept
        -> NGIN::Core::CoreResult<void> override {
        return context.RegisterSingletonValue<bool>("App.Ready", true);
    }
};

int main(int argc, char** argv) {
    auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);
    builder->SetApplicationName("App")
        .AddDefaultServices()
        .AddConfiguration()
        .AddModule<Startup>("App.Startup");

    auto app = builder->Build();
    if (!app) {
        return 1;
    }
    return app.Value()->Run().HasValue() ? 0 : 2;
}
```

The runnable [Hello.Hosted](../../Examples/Hello.Hosted) example shows the
package dependency, staged configuration, and static startup module together.

## Runtime model

NGIN.Core provides:

- application startup, ticking, stop requests, and shutdown;
- singleton, scoped, and transient services;
- static and dynamic modules;
- typed immediate and deferred events;
- task-runtime lanes;
- layered configuration;
- `NGIN.Log` integration.

A module participates in application lifecycle and services. A plugin is a
trusted native artifact that can register one or more modules and carry their
resources.

## Dependency injection

Services can be registered by value, factory, concrete type, or interface
mapping. Typed constructor injection does not require reflection. Reflection is
an optional feature for applications that need runtime metadata-driven
construction.

See [Dependency injection](../../docs/guides/ngin-core-di.md) for lifetimes,
scopes, constructor binding, and diagnostics.

## Dynamic plugins

Dynamic modules use a descriptor plus a native library exporting a registrar:

```cpp
extern "C" NGIN::Core::CoreResult<void>
NGIN_RegisterPlugin(NGIN::Core::IPluginModuleRegistry& registry);
```

Plugins are in-process native code and must be built for a compatible compiler
and runtime ABI. Hot reload, sandboxing, signature verification, and stable
cross-compiler ABI compatibility are not currently provided.

## Build and test

From the repository root:

```bash
cmake -S Packages/NGIN.Core -B build/ngin-core-ci \
  -DNGIN_CORE_BUILD_TESTS=ON \
  -DNGIN_CORE_BUILD_EXAMPLES=OFF
cmake --build build/ngin-core-ci --config Release --target NGINCoreTests
ctest --test-dir build/ngin-core-ci --output-on-failure -C Release
```

More detail is available in the [architecture notes](docs/Architecture.md).
