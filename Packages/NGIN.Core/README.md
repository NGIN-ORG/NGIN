# NGIN.Core

`NGIN.Core` is the optional hosted runtime package for NGIN applications. Plain
native projects do not need it. Use it when an application wants a host with
modules, services, configuration, lifecycle events, task lanes, logging, and
dynamic plugin loading.

## Hosted App Quickstart

The common path is `ApplicationBuilder`:

```cpp
#include <NGIN/Core/Core.hpp>

class StartupModule final : public NGIN::Core::IModule {
public:
  auto OnStart(NGIN::Core::ModuleContext& context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    return context.RegisterSingletonValue<bool>("App.Ready", true);
  }
};

int main(int argc, char** argv) {
  auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);
  builder->UseProjectFile("App.nginproj")
      .SetApplicationName("App")
      .AddDefaultServices()
      .AddConfiguration()
      .AddModule<StartupModule>("App.Startup");

  auto app = builder->Build();
  if (!app) {
    return 1;
  }
  return app.Value()->Run().HasValue() ? 0 : 2;
}
```

The smallest repository example is
[`../../Examples/Hello.Hosted`](../../Examples/Hello.Hosted/README.md). It
loads staged config, registers a static module implementation, and lets the V4
project manifest select that module.

## Runtime Model

`NGIN.Core` provides:

- kernel lifecycle orchestration
- static and dynamic module resolution
- service registry with singleton, scoped, and transient lifetimes
- immediate and deferred typed event bus
- task runtime lanes
- layered configuration store
- `NGIN.Log` integration

Static modules are registered directly from C++ with `AddModule<T>()` or
`AddModule(name, options, factory)`. Dynamic modules are discovered from
`.module.xml` / `.plugin-module.xml` descriptors and loaded from in-process
plugin libraries that export a registrar function.

## Dynamic Plugins

Dynamic plugin descriptors name the runtime module plus the binary that can
register its factory:

```xml
<Module Name="App.Plugin"
        Library="App.Plugin.dll"
        Registrar="NGIN_RegisterPlugin"
        Version="0.1.0"
        CompatiblePlatformRange=">=0.1.0 &lt;1.0.0" />
```

`Registrar` is optional and defaults to `NGIN_RegisterPlugin`. The exported
function has this shape:

```cpp
extern "C" NGIN::Core::CoreResult<void>
NGIN_RegisterPlugin(NGIN::Core::IPluginModuleRegistry& registry);
```

The registrar calls `registry.Register(moduleName, factory)`. The host keeps
loaded plugin libraries alive for the kernel lifetime. Hot reload, sandboxing,
signature verification, and cross-compiler ABI stability are outside the
current contract.

## Build and Test

From the repository root:

```bash
cmake -S Packages/NGIN.Core -B build/ngin-core-ci \
  -DNGIN_CORE_BUILD_TESTS=ON \
  -DNGIN_CORE_BUILD_EXAMPLES=OFF

cmake --build build/ngin-core-ci --config Release --target NGINCoreTests
ctest --test-dir build/ngin-core-ci --output-on-failure -C Release
```
