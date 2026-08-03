# NGIN.UI Developer Documentation

NGIN.UI is a retained, backend-neutral C++23 desktop UI toolkit. Start with one
of the runnable paths, then use the topic guides as needed.

## Start here

- [Standalone first window](ngin-ui-first-window.md): SDL3 + SDL_GPU in five
  minutes.
- [NGIN.Core hosted first window](ngin-ui-hosted-first-window.md): the same
  view under the Core host and dispatcher.
- [Gallery catalogue](../../Examples/NGIN.UI.Gallery/): every public control,
  state, and diagnostics surface in a native window.
- [Headless gallery](../../Examples/NGIN.UI.Gallery.Tests/): deterministic
  application-level tests without SDL or a GPU.

## Application model

- [Complete application composition: DI, pages, navigation, lifetimes, and tests](ngin-ui-application-composition.md)
- [Buildable multi-page hosted application](../../Examples/NGIN.UI.MultiPage/)
- [MVVM architecture and complete app organization](ngin-ui-mvvm.md)
- [MVVM commands, async actions, cancellation, errors, and command-bound buttons](ngin-ui-mvvm-commands.md)
- [Read-only and computed state, batching, field validation, and form summaries](ngin-ui-state-validation.md)
- [ViewModel task lifetime, keyed activation, and async screen states](ngin-ui-viewmodel-lifetime.md)
- [Composition, keys, reconciliation, layout, DPI, scrolling, state, bindings,
  validation, invalidation, ownership, and testing](ngin-ui-application-model.md)
- [Input, focus, commands, clipboard, IME, themes, resources, semantics,
  inspector tooling, and UI-thread rules](ngin-ui-input-accessibility-tooling.md)
- [Windows UI Automation and the Narrator checklist](ngin-ui-windows-accessibility.md)
- [Styling and visual states](ngin-ui-styling.md)
- [Desktop layout with Grid, WrapPanel, and Canvas](ngin-ui-desktop-layout.md)

## Controls and content

- [Foundational controls](ngin-ui-foundational-controls.md)
- [Collections and navigation](ngin-ui-collections-navigation.md)
- [Motion](ngin-ui-motion.md)
- [Multiline text, TextArea, font fallback, and images](ngin-ui-richer-content.md)
- [Composite and custom-painted controls](ngin-ui-custom-controls.md)

## Integration and support

- [Version 0.4 release and migration notes](ngin-ui-v0.4-release.md)
- [Version 0.2 release notes, migration, budgets, and demo archive](ngin-ui-v0.2-release.md)
- [Platform and renderer backend authoring](ngin-ui-backend-authoring.md)
- [Testing, visual baselines, performance budgets, packaging, and release gates](ngin-ui-testing-and-release.md)
- [Troubleshooting](ngin-ui-troubleshooting.md)
- [Generated API reference](../api/ngin-ui-mainpage.md)
- [Source compatibility and deprecation policy](../policies/ngin-ui-source-compatibility.md)
- [Project manifest authoring](nginproj-authoring.md)

## Fast verification

Build and run the gallery:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --profile Debug `
  --output build/manual/NGIN.UI.Gallery

build/manual/NGIN.UI.Gallery/bin/NGIN.UI.Gallery.exe
```

Run all gallery pages without leaving a window open:

```powershell
build/manual/NGIN.UI.Gallery/bin/NGIN.UI.Gallery.exe --smoke
```

The package overview and direct CMake test commands are in
[`Packages/NGIN.UI/README.md`](../../Packages/NGIN.UI/README.md).
