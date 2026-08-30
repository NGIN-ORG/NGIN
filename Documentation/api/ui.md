---
title: NGIN.UI API
description: Applications, windows, composition, runtime trees, layout, controls, input, state, ViewModels, styling, rendering, accessibility, backends, and testing.
---

# NGIN.UI API

**Include:** `<NGIN/UI/UI.hpp>`  
**Package:** `NGIN.UI`  
**Namespace:** `NGIN::UI`

NGIN.UI core is backend-neutral. Platform windows, native rendering, hosted
integration, and platform accessibility are separate packages.

## Packages

| Package | Public role |
| --- | --- |
| `NGIN.UI` | Composition, runtime tree, layout, controls, state, rendering contracts, semantics, and headless tests |
| `NGIN.UI.Backend.SDL3` | SDL3 windows, events, and rendering |
| `NGIN.UI.Hosting` | NGIN.Core lifecycle and service integration |
| `NGIN.UI.Accessibility.Windows` | Windows UI Automation provider |

The core has no public SDL type dependency.

## Application and windows

`ApplicationCreateInfo` supplies the platform and render backends plus
application identity. `Application` owns backend lifetime, creates windows,
and coordinates the event loop. `Window` owns one composition, layout,
rendering, and input pipeline. `DialogWindow` adds owner/modal behavior.

Central `Window` operations configure title/size/content, show/hide/close,
invalidate or schedule work, and attach event handling. The content callback
receives a `Composer` for each composition pass. Do not retain that composer;
it represents one ephemeral declaration pass.

An `Application`, its backends, and a window's authored state must outlive
work scheduled through that window. UI-owned mutation is expected on the UI
thread unless an API explicitly documents thread-safe posting.

## Composition and reconciliation

`Composer` builds a declarative tree. `ElementScope` closes nesting by scope.
`ElementDeclaration` contains a stable key, `ElementType`, properties, and
children. The runtime reconciles declarations against the previous keyed tree.

Keys identify sibling state across composition. Use stable domain identity,
not an array position that changes after insert/remove. Duplicate sibling keys
are an authoring error. A changed type under the same key can require node
replacement and loss of control-local state.

`RuntimeTree` and its node/handle APIs are runtime infrastructure. Prefer the
composer and control helpers in application code; use runtime access for custom
controls, diagnostics, and backends.

## Elements and layout

`NodeProperties` groups layout, interaction, scrolling, popup, text-field,
text, image, custom-element, visual, semantic, and related authored data.

`LayoutProperties` controls sizing, margin/padding, alignment, and flex-like
layout. Focused layout primitives cover constraints, lengths, Grid tracks,
WrapPanel, Canvas, scrolling, and geometry.

`LayoutEngine` measures and arranges a reconciled tree in logical units.
`LayoutPassStats`, `GridLayoutDiagnostics`, and
`WrapPanelLayoutDiagnostics` explain resolved output.

Measurement must be free of external side effects: the engine can measure more
than once. Keep physical pixels out of authored layout; the backend/window scale
maps logical units to device pixels.

## Controls

Control helpers cover text, button, checkbox, radio selection, slider,
progress, text fields/editors, image, separators, scrolling, popups, tooltips,
menus, dialogs, lists, and virtualized collections. Supporting values include
`CheckState`, `ControlPresentation`, `RadioSelection`, `SliderRange`, and
`ProgressValue`.

Control callbacks can trigger state changes and another composition. Do not
capture references to temporary composition data. Place persistent values in
state, a ViewModel, or another owner whose lifetime exceeds the callback.

## Input, focus, and commands

`InputRouter` normalizes and routes platform pointer, keyboard, text, focus,
popup, and command input. `InputDispatchResult` reports what changed or was
handled. Routed events have preview/tunnel and bubble behavior where supported.

Text input and key input are different: character/IME text belongs to the text
editing path, not key-code translation. Focus and pointer capture are runtime
state and are released when the target disappears or becomes ineligible.

Commands expose availability and execution independently of a particular
button or key binding. Keep a command's can-execute state observable when UI
presentation depends on it.

## Observable state

| Type | Purpose |
| --- | --- |
| `State<T>` | UI-thread-owned observable mutable value with validation/invalidation |
| `ReadOnlyState<T>` | Copyable read-only observable view |
| `StateBinding<T>` | Copyable read/write binding over state or custom accessors |
| `ComputedState<T>` | Read-only value recomputed from explicit dependencies |
| `StateBatch` | Defers notifications to the outer batch boundary |
| `Subscription` | Move-only observer lifetime token |

Keep the returned `Subscription` alive for as long as observation is wanted.
Destroy it before captured objects. `ComputedState` dependencies are explicit;
reading hidden external values does not make them observable. Use
`SubscriptionDiagnostics` to detect leaked subscriptions in tests.

## ViewModels and navigation

`ViewModelHost` creates, activates, reuses, and releases keyed plain
ViewModels. `ViewModelServiceResolver` is an optional non-owning hook for
application services.

`ViewModelTaskScope` owns async work for one mounted ViewModel, schedules UI
observation, exposes `ViewModelTaskStatus`, and cancels/drains work during
unmount. `ViewModelTaskHandle` cancels one operation. Task cancellation remains
cooperative; work should suspend/check through its `TaskContext`.

Navigation APIs model routes, stacks, and transitions. Collection APIs and
virtualization keep item identity separate from visible indices. A recycled
visual must be rebound completely before display.

## Style, themes, and motion

`VisualStyle`, `VisualStylePatch`, `VisualStateStyles`, `FocusVisual`, and
`VisualProperties` resolve state-specific presentation. `VisualStateFlags`
cover interaction and validation state. `Theme` and resource APIs provide
application/component lookup.

Motion APIs schedule transitions and animations through the UI timeline.
Animating layout-affecting properties can trigger repeated layout; prefer
render-only properties where equivalent and honor reduced-motion policy.

## Rendering and backends

`DisplayList` is the backend-neutral drawing command list. `UIRenderer`
converts the runtime tree into a `RenderPacket` containing vertices, indices,
batches, clips, textures, and uploads.

`IRenderBackend` owns surfaces, textures, updates, and frame submission.
Backends negotiate `BackendContractVersion` and advertise capability flags.
Validate version/capabilities before creating windows. A successful compile
does not mean the chosen runtime backend implements every optional feature.

## Accessibility

Semantics project the runtime tree into stable immutable snapshots.
`IAccessibilityBackend` consumes snapshots without accessing the mutable
runtime tree. Snapshot diffs report added, removed, changed, focused, selected,
and live-region nodes. Provider actions return through the thread-safe
`IAccessibilityActionSink` for UI-thread execution.

Custom controls must provide role, accessible name/value/state, focusability,
and actions appropriate to their behavior. A visual label is not automatically
an accessible name unless the semantic relationship is authored.

## Testing

- `TestPlatformBackend` supplies deterministic windows/events.
- `RecordingRenderBackend` deep-copies render packets and texture updates for
  assertions.
- `SoftwareRenderBackend` produces deterministic pixels.
- Navigation testing helpers exercise route state.
- Inspector and diagnostic snapshots expose tree, layout, focus, semantics,
  rendering, and invalidation evidence.

Headless tests should assert behavior and semantic output before relying on
pixel snapshots. Recording backend data is owned by the backend and remains
valid according to its recorded-surface lifetime.

## Errors

Fallible UI operations return `UIResult<T>` with structured `UIError`.
`UIErrorCode` covers invalid arguments/state, wrong-thread access, unavailable
backends, window/surface creation, rendering, resources, text shaping,
unsupported operations, and out-of-memory. Preserve `backend`, `operation`,
`logicalResource`, `nativeCode`, and `message` when surfacing the failure.

**Source:** [`NGIN.UI` public headers](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.UI/include/NGIN/UI)
