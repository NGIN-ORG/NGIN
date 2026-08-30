---
title: NGIN.UI C++ API
description: Public application, composition, layout, control, state, rendering, backend, and accessibility symbols.
---

# NGIN.UI C++ API

**Header:** `<NGIN/UI/UI.hpp>`  
**Namespace:** `NGIN::UI`  
**Target:** `NGIN::UI`  
**Source:** [Packages/NGIN.UI/include/NGIN/UI](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.UI/include/NGIN/UI)

## Application and composition

| Area | Central symbols | Header |
| --- | --- | --- |
| Application | `Application`, `Window`, application/window options | `Application.hpp` |
| Composition | `Composer`, `Element`, element keys/properties | `Composer.hpp`, `Element.hpp` |
| Runtime tree | `RuntimeTree`, `RuntimeNode`, `Reconciler` | `RuntimeTree.hpp` |
| Layout | `LayoutEngine`, geometry and constraint types | `Layout.hpp`, `Geometry.hpp` |
| Controls | button, text, selection, progress, tooltip primitives | `Controls.hpp` |

## State and interaction

`State<T>`, `ReadOnlyState<T>`, `StateBinding<T>`, `ComputedState<T>`,
`StateBatch`, and `Subscription` provide observable state. `InputRouter`, routed
pointer/key/text events, `Command`, `AsyncCommand`, navigation services, and
view-model task scopes coordinate interaction.

## Rendering and platform contracts

| Area | Central symbols |
| --- | --- |
| Rendering | `DisplayList`, `DisplayListBuilder`, `UIRenderer`, `PreparedRenderPacket` |
| Platform | platform and render backend interfaces, opaque handles, capability contracts |
| Images/text | image resources/caches, native text, editing buffers, direction |
| Accessibility | semantics, snapshots, actions, `IAccessibilityBackend` |
| Testing | `TestPlatformBackend`, `RecordingRenderBackend`, `SoftwareRenderBackend` |

UI state and tree mutation are UI-thread owned unless an API explicitly says
otherwise. Handles are opaque identities, not portable native pointers.
Backend packages negotiate capabilities separately from the core API.

See the [UI API guide](../../../api/ui.md) for subsystem contracts and package
selection.

