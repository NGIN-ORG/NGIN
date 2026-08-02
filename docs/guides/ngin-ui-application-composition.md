# NGIN.UI Application Composition

This guide is the shortest complete path from services to a multi-page app.
The buildable source is
[`Examples/NGIN.UI.MultiPage`](../../Examples/NGIN.UI.MultiPage/).

## Who owns what

- Core owns singleton, application, window, page, and transient service
  lifetimes.
- `PageRegistry` owns page metadata and the typed connection between a page
  tag, ViewModel, parameter, factory, and synchronous View function.
- One `NavigationService` owns one window-local or named-region stack.
- A View owns controls and layout by composing them into `Composer`.
- A ViewModel owns state, commands, validation, and asynchronous page work.
- `HostedViewModelHost<T>` cancels and observes ViewModel work before ending
  the Core page scope.

There is no process-global service locator or current page.

## Register services

Reflection-free constructor injection is the default. Declare the exact
ordered dependencies and use `NGIN::Memory::Shared<T>` parameters:

```cpp
class HomeViewModel {
public:
  using Dependencies = Core::ServiceDependencies<CustomerService>;
  explicit HomeViewModel(Memory::Shared<CustomerService> customers);
};

auto pages = PageRegistry{};
auto ui = Hosting::ConfigureUIPages(*builder, pages);
ui.Services().AddScoped<CustomerService>();
```

Instances and explicit factories have higher precedence and remain useful for
configuration or test doubles:

```cpp
ui.Services().AddSingleton<IClock>(Memory::MakeShared<TestableClock>());
ui.Services().AddScoped<CustomerService>(
    [](Core::ServiceResolutionContext& context) {
      auto clock = context.services.ResolveRequired<IClock>(context.scope);
      if (!clock) return Core::CoreResult<Memory::Shared<CustomerService>>{
          Utilities::Unexpected<Core::KernelError>(clock.Error())};
      return Memory::MakeShared<CustomerService>(clock.Value());
    });
```

## Optional reflected construction

Enable the Core `Reflection` package feature and register exactly one
injectable constructor. Reflection is optional; it does not change page or
View code.

```cpp
void NginReflect(Reflection::Tag<DetailViewModel>,
                 Reflection::TypeBuilder<DetailViewModel>& type) {
  type.InjectableConstructor<Memory::Shared<CustomerService>,
                             Memory::Shared<NavigationActions>>();
}

Reflection::ModuleRegistration module{"MyApp.Reflection"};
module.RegisterType<DetailViewModel>();
module.Commit();
```

`NGIN_INJECT` and `NGIN_DEPENDENCY(...)` can emit the same constructor
metadata through MetaGen. Version 0.4 intentionally keeps page registration
manual: the compose function and lifetime choice are product decisions, and
the explicit code is readable, deterministic, and removable. MetaGen is an
ergonomic option for constructor metadata only.

## Register typed pages

A page tag is a compile-time selector. Its string ID is stable diagnostics or
storage metadata. The optional route name is a lookup name, not a URL parser.

```cpp
struct HomePage {};
struct DetailPage {};
struct DetailParameter { CustomerId id; };

ui.AddPage<HomePage, HomeViewModel>(
    {.id = "home", .displayName = "Home", .routeName = "home"},
    [](Composer& composer, HomeViewModel& vm,
       const NoNavigationParameter&) {
      ComposeHome(composer, vm);
    });

ui.AddPage<DetailPage, DetailViewModel, DetailParameter>(
    {.id = "detail", .displayName = "Detail", .routeName = "customer"},
    [](Composer& composer, DetailViewModel& vm,
       const DetailParameter& parameter) {
      ComposeDetail(composer, vm, parameter);
    });
```

Duplicate tags, IDs, and route names fail registration. Factory and View
signatures are checked by the compiler. The registry freezes when its first
navigation stack is created.

`AddPage` registers the ViewModel as transient. Use `UsePage` when the service
already has a named or custom Core registration.

## Create a hosted stack

In a presentation module's `OnStart`, create a hosted window, activation
context, and one stack per window or named region:

```cpp
auto window = hosting.Value().services->CreateWindow(windowInfo);
auto activation = CreateHostedNavigationContext(hosting.Value(), window.Value());

NavigationService navigation{pages, activation.Value(), {
    .region = "Main",
    .cacheCapacity = 2,
    .isOnScheduler = [&] { return dispatcher->IsCurrentThread(); },
    .invalidate = [&](InvalidationKind kind) {
      window.Value().UI()->Invalidate(kind);
    },
}};
NavigationHost host{navigation};
window.Value().UI()->SetContent(
    [&](Composer& composer) { host.Compose(composer); });
window.Value().UI()->SetEventHandler(
    [&](const PlatformEvent& event) {
      static_cast<void>(host.HandleEvent(event));
    });
```

The dispatcher binds its UI thread when the run loop starts. Schedule the
initial page there, then mutate the stack with typed calls from UI callbacks:

```cpp
dispatcher->Post([&] { navigation.Start<HomePage>(); });
navigation.Navigate<DetailPage>(DetailParameter{customerId});
navigation.Replace<HomePage>();
navigation.Back();
navigation.Clear();
```

Activation completes before a push or replacement changes the stack. A
missing service or failed constructor therefore leaves the visible page
mounted. Reentrant operations and calls outside the configured UI scheduler
return structured errors.

Every live entry keeps a stable composition key. Covered stack entries stay
collapsed but retained, preserving keyed controls, focus, semantics, scroll
position, and local state for Back. Removed-page caching is disabled by
default. Reuse requires both a nonzero bounded capacity and an explicit cache
key.

## Async ViewModels and teardown

Views stay synchronous. A ViewModel can start owned work during activation:

```cpp
auto DetailViewModel::ActivateAsync(Async::TaskContext& context)
    -> ViewModelTaskScope::Task {
  loading.Set(true);
  co_await LoadCustomer(context);
  loading.Set(false);
}
```

When an entry leaves the stack, the hosted path performs this order:

1. reject new work;
2. call `Deactivate()`;
3. cancel and observe the active `ViewModelTaskScope`;
4. observe optional `DeactivateAsync()` cleanup;
5. release the ViewModel;
6. end the Core page scope and its scoped services.

Window close and application shutdown use the same sequence.
Let the presentation module own `NavigationHost` and `NavigationService`, and
release them in its `OnStop`; presentation modules stop before the hosted UI
module drains its application and window scopes.

## Standalone factories

NGIN.UI does not depend on Core. Register the same page and View with an
explicit factory and any `PageActivationContext` owned by the app:

```cpp
pages.Register<HomePage, HomeViewModel>(
    {.id = "home"},
    [&](PageActivationContext&, const NoNavigationParameter&, std::string_view) {
      return UIResult<PageLease<HomeViewModel>>{PageLease<HomeViewModel>{
          .viewModel = std::make_shared<HomeViewModel>(fakeCustomers)}};
    },
    [](Composer& composer, HomeViewModel& vm,
       const NoNavigationParameter&) { ComposeHome(composer, vm); });
```

The registry, stack, parameters, and View are identical in hosted,
standalone, and headless products. Only the factory/activation context changes.

## Headless tests and substitution

`NGIN::UI::Testing::PageTestContext` provides typed service overrides and
counts page leases. `NavigationTestDriver` selects an initial page and checks
the ordered stack.

```cpp
Testing::PageTestContext context;
context.Override<IClock>(std::make_shared<FakeClock>());

NavigationService navigation{pages, context};
Testing::NavigationTestDriver test{navigation};
test.SelectInitial<HomePage>();
test.AssertStack({"home"});

navigation.Clear();
REQUIRE(context.AssertNoScopeLeaks());
```

Use `TestPlatformBackend` and `RecordingRenderBackend` when a test also needs a
Window, input routing, layout, semantics, or rendering without SDL or a GPU.

## Diagnostics

`PageRegistry::Pages()` exposes immutable registered metadata.
`NavigationService::Snapshot()` reports the named region, ordered live stack,
visible entry, cache entries, stable entry IDs, and cache keys. Core service
diagnostics report page scopes, activation/failure counts, and cached service
instances. ViewModel hosts report active and cleanup task status.

The Gallery Diagnostics page presents these surfaces together.
