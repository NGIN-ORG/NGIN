# NGIN.UI Hosting

`NGIN.UI.Hosting` is the optional bridge between standalone `NGIN.UI` and the
`NGIN.Core` application host. It depends on the UI and Core contracts but not
on SDL or another concrete backend.

`ConfigureUIHosting()`:

- creates a hosted UI application and native text system from injected backend
  instances;
- installs an event-driven `IHostRunLoop`;
- registers the `NGIN.UI.Runtime` platform-stage module;
- publishes application, window-manager, dispatcher, platform-backend, and
  render-backend service references;
- publishes `HostedUIServiceProvider`, which creates an application scope,
  one scope per hosted window, and child page/activation scopes;
- drains posted UI work on the UI thread and wakes the platform wait whenever
  work or a Core stop request arrives.

Worker tasks can post completion work through `IUIDispatcher::Post()`. The
dispatcher swaps its pending queue before invoking callbacks, so callbacks that
post more work are deferred to the next UI iteration instead of recursively
running.

The concrete backend remains application-selected:

```cpp
auto registration = NGIN::UI::Hosting::ConfigureUIHosting(
    *builder,
    {
        .application = {
            .platform = NGIN::UI::SDL3::CreatePlatformBackend(),
            .renderer = NGIN::UI::SDL3::CreateRendererBackend(),
        },
    });
```

## Scoped ViewModels

Register services and ViewModels through the normal Core builder, then create
windows and page scopes through the returned hosting registration:

```cpp
builder->Services()
    .AddScoped<CustomerRepository>()
    .AddTransient<CustomerPageViewModel>();

auto hosting = ConfigureUIHosting(*builder, info);
auto app = builder->Build();
app.Value()->Start();

auto window = hosting.Value().services->CreateWindow(windowInfo);
auto page = window.Value().CreatePageScope("Customers");
HostedViewModelHost<CustomerPageViewModel> host{
    hosting.Value().runtime->UI().CreateTaskContext(*window.Value().UI()),
    std::move(page).Value()};
auto viewModel = host.Mount();
```

`HostedViewModelHost<T>` keeps the Core `Shared<T>` owner. On replacement,
window closure, or application shutdown it blocks new work, calls
`Deactivate()`, cancels and observes the ViewModel task scope, runs optional
`DeactivateAsync()`, releases the ViewModel, and only then ends the page DI
scope. Service-resolution failures remain structured `KernelError` values.

`ToStdShared()` and `ToCoreShared()` provide aliasing ownership bridges when an
existing API uses `std::shared_ptr`; neither bridge exposes a borrowed pointer
as the lifetime owner.

Standalone and headless applications continue to use
`KeyedViewModelHost<T>` and explicit `ViewModelFactory<T>` functions without
linking NGIN.Core or this package.
