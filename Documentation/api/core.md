---
title: NGIN.Core API
description: Application hosting, modules, services, events, task lanes, configuration, plugins, and structured errors.
---

# NGIN.Core API

**Include:** `<NGIN/Core/Core.hpp>`  
**Package:** `NGIN.Core`  
**Namespace:** `NGIN::Core`

NGIN.Core is optional. Use it when a product needs a hosted lifecycle,
dependency injection, modules, events, task lanes, layered configuration,
logging, or native plugins. A plain NGIN executable does not need the host.

## Build an application host

```cpp
auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);
builder->SetApplicationName("Editor")
    .SetProfile("Development")
    .AddDefaultServices()
    .AddConfiguration()
    .AddLogging()
    .AddModule<EditorModule>("Editor.Main");

auto application = builder->Build();
if (!application) return Report(application.Error());

auto ran = application.Value()->Run();
return ran.HasValue() ? 0 : Report(ran.Error());
```

`ApplicationBuilder` also provides `AddConfigSource`,
`AddPluginSearchPath`, `EnableDynamicPlugins`, `UseRunLoop`, `UseFileSystem`,
and collection access through `Services`, `Packages`, `Modules`, `Plugins`, and
`Configuration`.

`Build()` validates configuration and returns
`CoreResult<std::shared_ptr<IApplicationHost>>`. It does not run the host.

## `IApplicationHost`

| Operation | Contract |
| --- | --- |
| `Start()` | Initialize and start the resolved module graph |
| `Run()` | Start if needed and enter the configured run loop |
| `Tick()` | Advance one host tick when the caller owns the loop |
| `RequestStop(reason)` | Request cooperative host shutdown |
| `IsStopRequested()` | Inspect the request state |
| `Shutdown()` | Stop and tear down modules/services |
| `GetStartupReport()` | Read warnings, module state, and startup diagnostics |
| `GetServices()` / `GetConfig()` | Access resolved runtime services/configuration |

Do not mix an externally owned tick loop and `Run()` unless the chosen
`IHostRunLoop` explicitly supports that composition.

## Modules

```cpp
class EditorModule final : public NGIN::Core::IModule {
public:
    auto OnRegister(NGIN::Core::ModuleContext& context) noexcept
        -> NGIN::Core::CoreResult<void> override;
    auto OnInit(NGIN::Core::ModuleContext& context) noexcept
        -> NGIN::Core::CoreResult<void> override;
    auto OnStart(NGIN::Core::ModuleContext& context) noexcept
        -> NGIN::Core::CoreResult<void> override;
    auto OnStop(NGIN::Core::ModuleContext& context) noexcept
        -> NGIN::Core::CoreResult<void> override;
    auto OnShutdown(NGIN::Core::ModuleContext& context) noexcept
        -> NGIN::Core::CoreResult<void> override;
};
```

`ModuleContext` exposes the resolved descriptor, module and plugin provenance,
module service scope, services, events, tasks, config, logging, and stop state.

Lifecycle order is register → init → start, then stop → shutdown in resolved
dependency order. A failing callback produces `ModuleLifecycleFailure` and the
host performs the applicable unwind. Keep callbacks `noexcept` as declared;
return a structured failure instead of throwing.

## Services

`ServiceLifetime` supports singleton, scoped, and transient services. Register
values, factories, concrete types, or interface-to-implementation mappings
through `ServiceCollection`/`IServiceRegistry`.

```cpp
context.RegisterSingletonValue<AppSettings>("Current", settings);
context.RegisterScoped<IRequestContext, RequestContext>();
context.RegisterTransient<ICommand, SaveCommand>();

auto service = context.Services().ResolveRequired<AppSettings>(
    context.ModuleScope(), "Current");
```

Typed constructor injection uses declared dependency metadata and does not
require reflection. `ResolveOptional` represents a missing registration;
`ResolveRequired` turns absence into a service error. Scope validation prevents
a longer-lived service from accidentally capturing a shorter-lived instance.
Diagnostics expose registered contracts, dependencies, factories, scopes, and
resolution paths.

## Events

`IEventBus` supports typed and raw channels. Typed records use `EventTraits<T>`
and `EventChannelName/Id`. Delivery can be immediate or queued to `Main`, `IO`,
`Worker`, `Background`, or optional `Render` queues.

Store every `EventSubscriptionToken` needed for later unsubscription. A module
scope is cleared during teardown; do not retain callbacks that borrow destroyed
module state outside that scope. Immediate delivery runs in the publisher's
call; queued delivery runs when the corresponding queue is flushed.

## Task runtime

```cpp
CoreResult<TaskId> Submit(TaskLane lane, TaskCallback callback) noexcept;
CoreResult<TaskId> ScheduleAfter(
    TaskLane lane, std::chrono::milliseconds delay, TaskCallback callback) noexcept;
CoreResult<void> Barrier(TaskLane lane) noexcept;
CoreResult<void> BarrierAll() noexcept;
CoreResult<bool> WaitIdle(std::chrono::milliseconds timeout) noexcept;
```

Lanes are `Main`, `IO`, `Worker`, `Background`, and optional `Render`. Check
`IsLaneEnabled` before relying on an optional lane. `Barrier` waits for work
accepted before the barrier; it is not a global ban on new producer activity.

## Configuration

`IConfigStore` layers values by `ConfigLayer`: defaults, host inputs,
environment, local override, command line, and runtime mutation. Reads return
the effective value; snapshots and change subscriptions support observation.
Keep secrets out of ordinary configuration diagnostics and logs.

## Dynamic plugins

A plugin descriptor identifies a library and optional registrar. The default
entry point is:

```cpp
extern "C" NGIN::Core::CoreResult<void>
NGIN_RegisterPlugin(NGIN::Core::IPluginModuleRegistry& registry);
```

Loaded libraries remain alive until kernel destruction. Plugins are trusted
in-process native code. The contract does not provide sandboxing, signature
verification, hot reload, or stable cross-compiler ABI compatibility.

## Errors

`CoreResult<T>` contains `KernelError`, which carries `code`, `subsystem`,
`module`, `message`, `dependencyPath`, and an optional nested cause. Important
codes include compatibility failures, missing dependencies, cycles,
capability conflicts, ordering violations, module factory/lifecycle failure,
service/event/task/config failure, plugin support, thread policy, schema
validation, and internal error.

Log the structured fields and cause chain. Do not replace them with only the
top-level message.

**Source:** [`NGIN.Core` public headers](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.Core/include/NGIN/Core)

