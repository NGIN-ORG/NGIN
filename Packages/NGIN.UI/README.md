# NGIN.UI

`NGIN.UI` is a backend-neutral C++23 toolkit for native application interfaces.
It owns composition, layout, controls, input, text, images, accessibility
semantics, and diagnostics without depending on a windowing API.

Current package version: `0.4.0`.

> [!WARNING]
> NGIN.UI is experimental. Read the
> [source compatibility policy](../../docs/policies/ngin-ui-source-compatibility.md)
> before depending on pre-1.0 API stability.

## Choose the packages

| Package | Role |
| --- | --- |
| `NGIN.UI` | Backend-neutral UI core |
| `NGIN.UI.Backend.SDL3` | Native windows and rendering through SDL3 |
| `NGIN.UI.Accessibility.Windows` | Windows UI Automation provider |
| `NGIN.UI.Hosting` | Optional integration with `NGIN.Core` |

The UI core has no public SDL dependency. It includes deterministic headless
backends for tests.

## Start here

1. [Create a standalone window](../../docs/guides/ngin-ui-first-window.md).
2. Browse the runnable [Gallery](../../Examples/NGIN.UI.Gallery).
3. Read the [developer guide](../../docs/guides/ngin-ui.md) for the topic map.
4. Use [application composition](../../docs/guides/ngin-ui-application-composition.md)
   when building a multi-page application.

For hosted applications, start with
[NGIN.UI with NGIN.Core](../../docs/guides/ngin-ui-hosted-first-window.md).

## Capabilities

- retained composition with keyed reconciliation;
- constraint-based layout, Grid, WrapPanel, Canvas, and scrolling;
- backend-neutral display lists and rendering contracts;
- Unicode shaping, fallback fonts, text editing, and IME coordination;
- pointer, keyboard, focus, drag-and-drop, and routed input;
- state, bindings, validation, commands, ViewModels, and navigation;
- standard controls, lists, menus, dialogs, popups, and virtualization;
- themes, visual states, motion, images, and custom controls;
- semantic accessibility trees and Windows UI Automation integration;
- deterministic headless testing, inspector snapshots, and diagnostics.

Focused guides cover [styling](../../docs/guides/ngin-ui-styling.md),
[custom controls](../../docs/guides/ngin-ui-custom-controls.md),
[MVVM](../../docs/guides/ngin-ui-mvvm.md),
[collections and navigation](../../docs/guides/ngin-ui-collections-navigation.md),
[motion](../../docs/guides/ngin-ui-motion.md), and
[testing](../../docs/guides/ngin-ui-testing-and-release.md).

## Third-party implementation dependencies

Native text and common image decoding privately use pinned FreeType, HarfBuzz,
and stb_image sources. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
Configure `NGIN_UI_FETCH_THIRD_PARTY=OFF` to require installed dependencies, or
disable native text and standard image formats with the documented CMake
options.

## Build and test

```bash
cmake -S Packages/NGIN.UI -B build/ngin-ui -DNGIN_UI_BUILD_TESTS=ON
cmake --build build/ngin-ui --target NGINUITests
ctest --test-dir build/ngin-ui --output-on-failure
```

Generate API documentation with:

```bash
cmake -S Packages/NGIN.UI -B build/ngin-ui-docs -DNGIN_UI_BUILD_DOCS=ON
cmake --build build/ngin-ui-docs --target NGINUIDocs
```

Release notes: [0.2](../../docs/guides/ngin-ui-v0.2-release.md),
[0.3](../../docs/guides/ngin-ui-v0.3-release.md), and
[0.4](../../docs/guides/ngin-ui-v0.4-release.md).
