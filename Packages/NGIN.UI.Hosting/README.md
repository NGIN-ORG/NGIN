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

## Typed Pages And Navigation

`ConfigureUIPages()` keeps service and page registration together while pages
remain owned by NGIN.UI, not Core. A page tag selects a registration at compile
time, and each registration fixes its ViewModel, parameter type, and
synchronous compose function:

```cpp
struct HomePage {};
struct CustomerPage {};
struct CustomerParameter { CustomerId id; };

NGIN::UI::PageRegistry pages;
auto ui = NGIN::UI::Hosting::ConfigureUIPages(*builder, pages);
ui.Services().AddScoped<CustomerRepository>();

ui.AddPage<HomePage, HomeViewModel>(
    {.id = "home", .displayName = "Home", .routeName = "home"},
    [](Composer& composer, HomeViewModel& vm,
       const NoNavigationParameter&) { ComposeHome(composer, vm); });
ui.AddPage<CustomerPage, CustomerViewModel, CustomerParameter>(
    {.id = "customer", .displayName = "Customer", .routeName = "customer"},
    [](Composer& composer, CustomerViewModel& vm,
       const CustomerParameter& parameter) {
      ComposeCustomer(composer, vm, parameter);
    });
```

After Core starts and creates a hosted window, bind one navigation context and
stack to it:

```cpp
auto context = CreateHostedNavigationContext(hosting.Value(), window.Value());
NavigationService navigation{pages, context.Value(), {
    .region = "Main",
    .cacheCapacity = 2,
    .isOnScheduler = [&] { return dispatcher->IsCurrentThread(); },
    .invalidate = [&](InvalidationKind kind) { window.Value().UI()->Invalidate(kind); },
}};
NavigationHost content{navigation};
window.Value().UI()->SetContent(
    [&](Composer& composer) { content.Compose(composer); });
window.Value().UI()->SetEventHandler(
    [&](const PlatformEvent& event) { static_cast<void>(content.HandleEvent(event)); });

navigation.Start<HomePage>();
navigation.Navigate<CustomerPage>(CustomerParameter{customerId});
navigation.Back();
```

Every live stack entry owns a separate Core page scope. A failed ViewModel
resolution does not change the stack. Pop, replace, clear, cache eviction,
window close, and application shutdown use the same deterministic ViewModel
task and scope teardown path described above. Removed-page caching is disabled
unless both a bounded capacity and an explicit cache key are supplied.
