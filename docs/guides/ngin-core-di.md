# NGIN.Core Dependency Injection

NGIN.Core can create services and pass their dependencies to their
constructors. Reflection is not required.

## Register And Resolve Services

Declare constructor dependencies with `ServiceDependencies`:

```cpp
#include <NGIN/Core/Core.hpp>

class IClock {
public:
  virtual ~IClock() = default;
  [[nodiscard]] virtual auto NowText() const -> std::string = 0;
};

class SystemClock final : public IClock {
public:
  [[nodiscard]] auto NowText() const -> std::string override {
    return "12:00";
  }
};

class HomeViewModel {
public:
  using Dependencies = NGIN::Core::ServiceDependencies<IClock>;

  explicit HomeViewModel(NGIN::Memory::Shared<IClock> clock)
      : m_clock(std::move(clock)) {}

private:
  NGIN::Memory::Shared<IClock> m_clock;
};

int main(int argc, char** argv) {
  auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);
  builder->Services()
      .AddSingleton<IClock, SystemClock>()
      .AddTransient<HomeViewModel>();

  auto app = builder->Build();
  if (!app) {
    return 1;
  }

  auto viewModel =
      app.Value()->GetServices()->ResolveRequired<HomeViewModel>();
  return viewModel ? 0 : 2;
}
```

Dependencies are resolved from the same active scope as the service being
created. They are resolved from left to right in the order listed by
`ServiceDependencies<T...>`.

## Lifetimes

- `AddSingleton<T>()` creates one instance and keeps it for the host lifetime.
- `AddScoped<T>()` creates one instance in each active scope.
- `AddTransient<T>()` creates a new instance for every request.

A singleton cannot depend on a scoped service. Resolution fails with the full
dependency path instead of keeping a page, request, or operation service alive
for too long.

The direct registry API can create and end explicit scopes:

```cpp
auto services = NGIN::Core::CreateServiceRegistry();
auto scope = services->BeginScope(
    NGIN::Core::ServiceScopeKind::Operation, "Import documents");

NGIN::Core::ServiceRegistrationOptions options{};
options.ownerScope = scope.Value();
if (!services->RegisterScoped<DocumentSession>(options)) {
  return 1;
}

auto session =
    services->ResolveRequired<DocumentSession>(scope.Value());
services->EndScope(scope.Value());
```

Do not keep a scoped service after its scope ends.

## How Construction Is Chosen

Automatic registration uses this order:

1. An instance or factory supplied by the application.
2. The constructor described by `T::Dependencies`.
3. A constructor that accepts `Shared<IServiceProvider>`.
4. A default constructor.

Use an explicit factory when construction needs a value, a named service, an
optional service, or application-specific logic:

```cpp
builder->Services().AddFactory<ReportWriter>(
    [](NGIN::Core::ServiceResolutionContext& context)
        -> NGIN::Core::CoreResult<NGIN::Memory::Shared<ReportWriter>> {
      auto output = context.services.ResolveRequired<IOutput>(
          "Reports", context.scope);
      if (!output) {
        return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
            output.Error());
      }
      return NGIN::Memory::MakeShared<ReportWriter>(output.Value());
    },
    NGIN::Core::ServiceLifetime::Transient);
```

Explicit factories and instances remain supported even when a type cannot be
constructed automatically.

## Errors And Diagnostics

Duplicate keys and duplicate named contracts fail during registration. Missing
dependencies, constructor cycles, invalid lifetime capture, and factory
failures return a `KernelError`. `dependencyPath` contains the service chain
that led to a resolution failure.

`IServiceProvider::Diagnostics()` returns a read-only snapshot containing:

- registered service keys and declared dependencies;
- lifetime, owner scope, and metadata;
- successful activation and failure counts;
- cached instance counts;
- active scopes.

Diagnostics do not create services. This makes them safe for tests and
developer tools.

## Named And Optional Services

Register a named service by passing its name to a registration overload and
resolve it with the same name. Use `ResolveOptional<T>()` when absence is valid.
`ServiceDependencies<T...>` describes required unnamed dependencies; use a
factory when a constructor needs a named or optional dependency.
