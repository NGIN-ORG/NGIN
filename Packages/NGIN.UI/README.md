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

### Application task ownership

Use `Application::BackgroundTasks().Spawn(...)` when a result handle is needed,
or `.Transfer(...)` to give an owned factory/cold task to the application. Check
admission failure; the bounded supervisor owns accepted work until retirement.
Unhandled errors close admission, request stop, and remain in the joined report.
`Application::Run()` joins application work before normal return. Applications
that drive `PumpOnce()` themselves call `ShutdownTasks()` while borrowed model,
window, and task resources are still alive. If event pumping fails, live work
remains owned; shutdown must be retried before destroying the application.
Inspect `BackgroundTasks().TakeResult()` for retained shutdown diagnostics.

The UI executor has bounded ordinary/timer queues, reserved continuation
admission, removable timers, and a 64-callback dispatch batch. Queue rejection
never invokes work inline. Already-admitted completion delivery remains available
after ordinary admission closes. Its platform clock controls timer wakeups.

The independent executor contract target can be built and run without linking
rendering/platform-provider implementation tests:

```bash
cmake --build build/ngin-ui --target NGINUIAsyncTests
ctest --test-dir build/ngin-ui --output-on-failure -R '^UI\.Async\.'
```

`AsyncCommand`, `ViewModelTaskScope`, and `KeyedViewModelHost` take an explicit
`TaskSupervisor<>&` before their `TaskContext`. `ValidationField::SetAsyncValidator`
takes the same owner/context pair. Supply `application.BackgroundTasks()` for UI
work. The owner must remain alive for every submission and through retirement;
borrowed view-model resources must remain alive until shutdown joins their work.
The context selects execution and local cancellation; supervision adds owner stop.
Sequential actions/validators are awaited within that owned task.

Admission rejection returns `CommandInvocation::RejectedOwner`, an invalid
view-model task handle with a fault status, or a validation issue. It clears
running/validating status. Queued runs retain their callable through cleanup.
Fallible setup finishes before running status is published. Stopped validation
remains unavailable. Errors arriving after destruction or supersession of a UI
observer reach the supervisor: infrastructure faults retain their original
classification; unobserved UI domain errors become diagnostic faults carrying
the UI error code/id and message. Expected stale validation results are discarded.

The independent ownership target exercises commands, validation, and view-model
work using their production implementation without rendering providers:

```bash
cmake --build build/ngin-ui --target NGINUIOwnershipTests
ctest --test-dir build/ngin-ui --output-on-failure -R '^UI\.Ownership\.'
```
