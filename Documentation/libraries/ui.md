---
title: NGIN.UI
description: Backend-neutral C++23 native interfaces with retained composition, layout, controls, input, text, accessibility, and testing.
---

# NGIN.UI

`NGIN.UI` is a backend-neutral C++23 toolkit for native application interfaces.
The core owns composition, layout, controls, input, text, images, accessibility
semantics, and diagnostics without exposing a windowing API dependency.

## Choose the packages

| Package | Role |
| --- | --- |
| `NGIN.UI` | Backend-neutral UI core |
| `NGIN.UI.Backend.SDL3` | Native windows and rendering through SDL3 |
| `NGIN.UI.Accessibility.Windows` | Windows UI Automation provider |
| `NGIN.UI.Hosting` | Optional integration with NGIN.Core |

## Start here

1. Open a native window in the [quick start](./ui/quick-start.md).
2. Learn [composition and layout](./ui/composition-layout.md).
3. Add [controls and input](./ui/controls-input.md).
4. Choose a state model in [state and MVVM](./ui/state-mvvm.md).
5. Use the [UI C++ reference](../reference/cpp/ui/index.md) for application, composition,
   state, backend, accessibility, and testing contracts.

## Topic map

| Area | Guide |
| --- | --- |
| Retained views, reconciliation, layout | [Composition and layout](./ui/composition-layout.md) |
| Standard controls, focus, keyboard, pointer | [Controls and input](./ui/controls-input.md) |
| State, bindings, validation, commands, navigation | [State and MVVM](./ui/state-mvvm.md) |
| Themes, visual states, images, custom controls | [Styling and motion](./ui/styling-motion.md) |
| Headless tests, semantics, diagnostics | [Testing and accessibility](./ui/testing-accessibility.md) |
| SDL3, Windows accessibility, Core integration | [Backends and hosting](./ui/backends-hosting.md) |

## Detailed guides

The topic pages above orient you. The [NGIN.UI guide library](./ui/guides/index.md)
contains full application walkthroughs for first windows, composition,
controls, collections, MVVM, validation, styling, motion, custom controls,
accessibility, testing, backend authoring, and troubleshooting.

Current package version: `0.4.0`. The API remains experimental before 1.0.

The core library is backend-neutral. Platform backends negotiate contract
versions and capabilities at runtime, so package availability and API
availability are separate checks.
