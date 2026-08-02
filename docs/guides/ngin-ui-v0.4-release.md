# NGIN.UI 0.4 release

Version 0.4 connects Core dependency injection, optional Reflection,
ViewModel lifetimes, typed pages, and navigation. The NGIN.UI core, SDL3
backend, hosting bridge, Windows accessibility provider, Gallery, and examples
are version `0.4.0`. Applications should request `>=0.4.0 <0.5.0` for the
NGIN.UI package family.

## What is new

- complete reflection-free Core constructor injection, interface mappings,
  named services, lifetime validation, and diagnostics;
- opt-in reflected constructor injection with ABI-owned service instances and
  safe module unload;
- application, window, page, and activation scopes in NGIN.UI.Hosting;
- owning Core `Shared<T>` ViewModel activation and deterministic async
  teardown;
- typed page tags, stable IDs, display/route names, parameters, and View
  composition;
- window-local and named-region startup, push, replace, back, clear, retained
  stack state, rollback, and bounded explicit caching;
- headless service overrides, initial-page selection, stack assertions, and
  page-scope leak checks;
- a buildable multi-page example and a Gallery migrated to the same public
  page/navigation APIs.

Start with the
[application-composition guide](ngin-ui-application-composition.md) and the
[`NGIN.UI.MultiPage`](../../Examples/NGIN.UI.MultiPage/) example.

## Migration from 0.3

Update NGIN.UI-family package ranges from `>=0.3.0 <0.4.0` to
`>=0.4.0 <0.5.0`.

Existing standalone Views, `State`, bindings, commands,
`KeyedViewModelHost<T>`, and explicit `ViewModelFactory<T>` code remain valid.
NGIN.UI still has no Core or Reflection dependency.

`ViewModelTaskScope` now has drain-aware `Close()` and `IsDrained()`. Existing
`CancelAll()` calls remain source-compatible. Prefer `Close(observer)` when a
service scope must outlive cancellation until every task completion is
observed.

Hosted apps can keep manually creating `HostedUIScope` and
`HostedViewModelHost<T>`. New apps should normally use `HostedPageBuilder` and
`HostedNavigationContext`, which apply the same teardown ordering
automatically.

Replace application-owned page enums/switches incrementally:

1. keep the enum only as a menu or compatibility adapter;
2. register each View with `PageRegistry::Register` or
   `HostedPageBuilder::AddPage`;
3. route selection through `NavigationService`;
4. compose through `NavigationHost`;
5. remove the old composition switch after tests use the registry path.

No string property bags, ViewModel base class, markup language, property
injection, or generated page registration were added.

## MetaGen decision

MetaGen constructor annotations remain supported and emit the same injectable
constructor metadata as handwritten `NginReflect` functions. Version 0.4 does
not generate page registrations. Page identity, navigation parameters,
composition, service naming, and caching are explicit product policy; manual
registration is clearer and can be removed without generated artifacts.

## Release gates

The release gate covers:

- Core DI with Reflection disabled and enabled;
- Reflection ABI invocation and unload ownership;
- NGIN.UI ViewModel and navigation lifetimes;
- NGIN.UI.Hosting page-scope teardown;
- public API documentation and install consumers;
- the standalone, hosted, and headless Gallery products;
- Windows, Linux, and macOS manifests and CI matrix definitions.

Build the Gallery archive with:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --profile Release `
  --output build/release/NGIN.UI.Gallery-0.4.0
```

Run `bin/NGIN.UI.Gallery.exe --page Diagnostics` to inspect page/navigation
state, or use `--smoke` to compose every page and exit.
