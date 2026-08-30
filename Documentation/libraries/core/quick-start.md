---
title: NGIN.Core quick start
description: Build a hosted application with default services and a startup module.
---

# NGIN.Core quick start

This application creates a host, registers a static module, and participates in
the application lifecycle.

## Before you start

You need the NGIN CLI, a C++23 compiler, and a workspace that discovers
`NGIN.Core`. Create `HostedApp/HostedApp.nginproj` and
`HostedApp/src/main.cpp` from the following two sections.

## Add the package

```xml
<Executable Name="HostedApp">
  <Uses>
    <Package Name="NGIN.Core" Version="0.1" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

## Create the application

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
    builder->SetApplicationName("HostedApp")
        .AddDefaultServices()
        .AddConfiguration()
        .AddModule<Startup>("HostedApp.Startup");

    auto app = builder->Build();
    if (!app) {
        return 1;
    }
    return app.Value()->Run().HasValue() ? 0 : 2;
}
```

## Build and run

```bash
ngin validate --project HostedApp/HostedApp.nginproj --configuration Debug
ngin build --project HostedApp/HostedApp.nginproj --configuration Debug
ngin run --project HostedApp/HostedApp.nginproj --configuration Debug
```

The host starts the module, registers `App.Ready`, shuts down normally, and
exits `0`. Build failure exits before launch; the example returns `1` for host
construction failure and `2` for run/lifecycle failure.

## If it fails

- Inspect `KernelError` instead of returning only an integer in real code; it
  carries subsystem, module, dependency path, and nested cause.
- Use `GetStartupReport()` when `Build` succeeds but startup does not.
- Confirm package/runtime configuration files are included in the StagePlan
  when the application depends on them.

The runnable `Examples/Hello.Hosted` product demonstrates staged configuration
and a static startup module together.

Continue with [application lifecycle](./application-lifecycle.md) and keep the
[Core C++ reference](../../reference/cpp/core/index.md) open for service and host contracts.
