# NGIN.Core DI Implementation Plan

## Summary

NGIN.Core service registration should move in phases from explicit typed factories to service-owned dependency declarations and optional reflection-backed constructor injection. The base runtime must remain usable without `NGIN.Reflection`; reflection should improve authoring ergonomics, not become required for normal hosted applications.

## Phase 1: Lazy Construction And Resolver Injection

- Add `IServiceProvider` as the resolve-only service surface.
- Keep `IServiceRegistry` as the mutating registration surface.
- Add lazy `AddSingleton<T>()`, `AddScoped<T>()`, and `AddTransient<T>()`.
- Support auto-construction for:
  - `T()`
  - `T(NGIN::Memory::Shared<IServiceProvider>)`
- Keep explicit typed factory overloads for advanced construction.

## Phase 2: Compile-Time Dependency Declarations

Add an optional service-owned dependency declaration:

```cpp
class GameService {
public:
  using Dependencies = NGIN::Core::ServiceDependencies<IConfigStore, WorldService>;

  GameService(
      NGIN::Memory::Shared<IConfigStore> config,
      NGIN::Memory::Shared<WorldService> world);
};
```

`AddTransient<GameService>()` should detect `GameService::Dependencies`, resolve each dependency from the current scope, and call the matching constructor. This keeps startup code focused on composition while avoiding reflection requirements.

## Phase 3: Reflection Constructor Metadata

When `NGIN.Reflection` is enabled, allow services to declare injectable constructors through reflection metadata:

```cpp
void NginReflect(NGIN::Reflection::Tag<GameService>,
                 NGIN::Reflection::TypeBuilder<GameService>& type) {
  type.Constructor<
      NGIN::Memory::Shared<IConfigStore>,
      NGIN::Memory::Shared<WorldService>>();
}
```

The DI bridge should read constructor parameter metadata and build the same internal typed factory used by compile-time dependency declarations.

## Phase 4: Reflection-Backed AddTransient<T>()

`services.AddTransient<T>()` should eventually choose construction in this order:

1. service-owned compile-time dependencies, if present
2. reflection injectable constructor metadata, if available
3. `T(NGIN::Memory::Shared<IServiceProvider>)`
4. `T()`

Ambiguous reflected constructors should fail with a clear service registration error. The initial reflection DI path should support only `NGIN::Memory::Shared<T>` constructor parameters.

## Design Rules

- Service dependencies retained by a service should use `NGIN::Memory::Shared<T>`.
- Raw references and raw pointers are not part of the first constructor injection contract.
- Scoped dependencies resolve against the construction scope.
- Singleton services are created lazily on first resolve.
- `NGIN::Utilities::Any<>` remains appropriate for event payloads and reflection values, not services.
