# NGIN.Core Refactoring Review

**Date:** 2026-04-03
**Scope:** Full review of `Packages/NGIN.Core/` — headers, sources, tests, build, and docs.

---

## What's Good

The design ambition is solid: a modular kernel with DI, events, tasks, config layering, and a resolver with real enforcement (family layers, stage ordering, version checks, cycle detection). The contracts are well-separated behind interfaces. The test coverage (~1720 lines) is meaningful. The `CoreResult<T>` error model with structured `KernelError` and nested causes is a strong foundation.

---

## Suggestions (ordered by impact)

### 1. Split Application.cpp (~2866 lines) into Focused Files

**Problem:** `Application.cpp` is a monolith containing: XML manifest parsing for projects, packages, modules, configurations, build descriptors; the builder implementation; the host implementation; and all the bootstrap/registrar machinery. This is the single biggest maintainability problem.

**Suggestion:** Split into:
- `ManifestParsing.cpp` / `ManifestParsing.hpp` (internal) — all the `ParseProjectManifest`, `ParsePackageManifest`, `ParseModuleDescriptor`, `ParseProfileDefinition` functions
- `ApplicationBuilder.cpp` — the builder implementation only
- `ApplicationHost.cpp` — the host implementation only

This also makes it testable in isolation. Right now you can't test manifest parsing without going through the builder.

---

### 2. Eliminate the HostType / Host category Duplication

**Problem:** `Types.hpp:14` defines `HostType` (GuiApp, Game, Editor, Service, ConsoleApp, TestHost) and `Application.hpp:27` defines `Host category` with the exact same values. There's a `ToHostType(Host category)` conversion function in Application.cpp. This is confusing for users — which one do they use?

**Suggestion:** Kill `Host category` entirely. Use `HostType` everywhere. The kernel and the builder should speak the same vocabulary.

---

### 3. Rethink the Event System — Make It Type-Safe

**Problem:** Events use string channels and `Any<>` payloads (`Events.hpp:82`). Users must know the magic string for a channel and the exact payload type to cast. This is error-prone and provides zero compile-time safety.

**Suggestion:** Introduce typed events:
```cpp
template<typename TEvent>
auto Subscribe(EventCallback<TEvent> callback, ...) -> Token;

template<typename TEvent>
auto Publish(TEvent event) -> CoreResult<void>;
```
The channel key can be derived from `TypeName<TEvent>`. Keep the untyped `Any<>` path as a low-level escape hatch for dynamic/plugin scenarios, but make the typed API the primary surface. This is one of the most user-facing improvements you can make.

---

### 4. Service Registry Needs Typed Ergonomics at the Interface Level

**Problem:** The current typed helpers (`RegisterTyped<T>`, `ResolveTypedOptional<T>`) are free functions that wrap the string-key + `Any<>` interface. The primary `IServiceRegistry` API is entirely string-keyed and untyped. Users have to manually deal with `Any<>` casting, and the free-function helpers aren't discoverable.

**Suggestion:** Add typed methods directly to `IServiceRegistry` as default-implemented template methods (non-virtual, calling through the virtual string-key API). Also add `ResolveRequired<T>()` to the typed surface — the current code only has `ResolveTypedOptional<T>()`.

Also consider: interface-based service registration like `services.Register<IWindowSystem, SDLWindowSystem>()` where the key is the interface type and the factory constructs the implementation. This is the DI pattern most C++ developers coming from C# / Java expect.

---

### 5. Modernize Error Propagation — Add Monadic Helpers or a Macro

**Problem:** The codebase is drowning in this pattern (hundreds of occurrences):
```cpp
auto result = SomeOperation();
if (!result) {
    return NGIN::Utilities::Unexpected<KernelError>(result.Error());
}
```
This is ~3 lines per fallible call. In `Application.cpp`, easily 40%+ of the code is just error propagation boilerplate.

**Suggestion:** Either:
- Add a `TRY` macro: `#define NGIN_TRY(expr) do { auto _r = (expr); if (!_r) return Unexpected(_r.Error()); } while(0)`
- Or add `.and_then()` / `.map()` monadic combinators to `Expected<T, E>`
- Or both

This would cut Application.cpp and Kernel.cpp size by 30-40% and make the actual logic visible.

---

### 6. Application.hpp Is a God Header — Separate Manifest Types

**Problem:** `Application.hpp` defines ~30 types: `ProjectManifest`, `PackageManifest`, `ProfileDefinition`, `ProjectBuildDescriptor`, `BuildSetting`, `OutputDefinition`, `RuntimeDefinition`, `PackageReference`, `PluginReference`, `ModuleSelection`, `PackageBootstrapContext`, `PackageBootstrapRegistry`, `ServiceCollection`, `PackageCollection`, `ModuleCollection`, `PluginCollection`, `ConfigurationBuilder`, `IApplicationHost`, `ApplicationBuilder`, etc. This is a lot of unrelated concepts in one header.

**Suggestion:** Split into:
- `Manifests.hpp` — `ProjectManifest`, `PackageManifest`, and all their sub-types
- `Builder.hpp` — `ApplicationBuilder`, the collection interfaces, `IApplicationHost`
- `Bootstrap.hpp` — `PackageBootstrapContext`, `PackageBootstrapRegistry`, bootstrap descriptors

Users who just want to build an app shouldn't need to include manifest parsing types.

---

### 7. The ModuleContext Deserves a Richer API

**Problem:** `ModuleContext` (`Module.hpp:22`) exposes raw subsystem references but only has convenience methods for `RegisterSingleton` and `RegisterFactory`. Modules can't easily:
- Subscribe to events (have to manually create `EventScope` with the right owner string)
- Submit tasks to specific lanes
- Read typed config values
- Resolve typed services

**Suggestion:** Add convenience methods:
```cpp
// Events
auto SubscribeEvent(std::string channel, EventCallback cb, Int32 priority = 0)
    -> CoreResult<EventSubscriptionToken>;

// Typed services
template<typename T>
auto Resolve() -> CoreResult<T>;

// Typed config
template<typename T>
auto GetConfig(std::string_view key, T defaultValue = {}) -> T;

// Task submission
auto SubmitTask(TaskLane lane, TaskCallback cb) -> CoreResult<TaskId>;
```

The `ModuleContext` is the primary API surface for module authors. Making it rich and ergonomic is high-leverage.

---

### 8. Platform Abstractions Don't Belong in Core

**Problem:** `Platform.hpp` defines `IWindowSystem` and `IInputSystem`. Earlier package manifests modeled these platform features as runtime modules. These are concrete platform concerns, not core kernel infrastructure.

**Suggestion:** Move `IWindowSystem`, `IInputSystem`, and `WindowDescriptor` out of NGIN.Core into a separate `NGIN.Platform` package (or at minimum a `Platform/` subdirectory with its own header). Core should only define the kernel, DI, events, tasks, config, and module lifecycle. Platform services should be *registered through* Core, not *defined by* Core.

---

### 9. Run Loop Is Primitive — Add a Tick/Frame Model

**Problem:** The kernel `Run()` loop (`Kernel.cpp:305-319`) is just:
```cpp
while (!m_stopRequested) {
    Tick();
    sleep_for(1ms);
}
```
`Tick()` only flushes deferred events. There's no frame timing, no fixed-timestep option, no update/render split, no way for modules to participate in the tick cycle.

**Suggestion:** Introduce a proper frame/tick model:
- `ITickable` interface that modules can implement for per-frame updates
- Fixed vs variable timestep policies
- Frame time / delta time tracking accessible from `ModuleContext`
- The kernel collects `ITickable` modules and calls them in order during `Tick()`

This is essential for Game/Editor/GuiApp host types. Without it, every module that needs per-frame work has to independently wire up its own scheduling through the task system.

---

### 10. Config Store Needs Typed Access and Section Support

**Problem:** Config is entirely string-to-string with manual conversion via `ConvertConfigValue<T>()`. There's no concept of config sections/objects, no deserialization into structs, no schema validation.

**Suggestion:**
- Add `GetValue<T>(key, defaultValue)` directly to `IConfigStore`
- Add section support: `GetSection("Graphics")` returning a sub-store or a snapshot of all keys under that prefix (the `Enumerate` method exists but it's low-level)
- Consider supporting structured config (JSON/TOML sections) rather than only flat key=value files

---

### 11. Tests in a Single File Won't Scale

**Problem:** All tests are in `AllTests.cpp` (~1720 lines). As you add features, this becomes unwieldy.

**Suggestion:** Split into one test file per subsystem: `VersioningTests.cpp`, `ConfigTests.cpp`, `ServicesTests.cpp`, `EventsTests.cpp`, `TasksTests.cpp`, `LoaderTests.cpp`, `KernelTests.cpp`, `ApplicationBuilderTests.cpp`.

---

### 12. Versioning Should Use std::strong_ordering

**Problem:** `SemanticVersion` and `VersionRange` work via `Contains()` and format/parse functions but lack comparison operators. You're on C++23 — use the spaceship operator.

---

### 13. Consider a Simpler "Quick Start" API

**Problem:** The simplest possible NGIN application requires: create builder, set name, set profile, optionally register modules, build, start, run, shutdown, handle errors at every step. Compare to frameworks like SDL or GLFW where you can get a window in 10 lines.

**Suggestion:** Add a minimal convenience entry point for the common case:
```cpp
int main(int argc, char** argv) {
    return NGIN::Core::Run(argc, argv, [](NGIN::Core::ApplicationBuilder& builder) {
        builder.SetApplicationName("MyApp")
               .UseProfile(HostType::Game);
        builder.Modules().Register(...);
    });
}
```
One function, one lambda, handles Build/Start/Run/Shutdown/error-reporting internally.

---

## Priority Ranking

If picking the top 5 most impactful changes:

1. **Error propagation macro/monadic helpers** (#5) — Immediately makes all existing code 30-40% shorter and all future code easier to write
2. **Split Application.cpp and Application.hpp** (#1, #6) — Unblocks maintainability for everything else
3. **Typed events** (#3) — Biggest user-facing API improvement
4. **Kill HostType/Host category duplication** (#2) — Quick win, removes confusion
5. **Richer ModuleContext** (#7) — Makes module authoring genuinely pleasant
