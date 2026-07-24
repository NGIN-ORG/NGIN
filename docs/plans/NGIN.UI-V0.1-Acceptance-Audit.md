# NGIN.UI Version 0.1 Acceptance Audit

Status: Complete
Proposal: [`../proposals/NGIN.UI-Proposal.md`](../proposals/NGIN.UI-Proposal.md)
Implementation plan: [`NGIN.UI-Implementation-Plan.md`](NGIN.UI-Implementation-Plan.md)
Audit date: 2026-07-24

## Scope

This audit covers the proposal's milestones 0 through 8 and its definition of
architectural success. Sections framed as eventual requirements, possible
later extractions, or version 0.1 non-goals remain future work and are not
silently treated as completed.

## Milestone Acceptance

| Milestone | Result | Evidence |
|---|---|---|
| 0 — Contracts and test harness | Complete | Backend-neutral geometry, handles, errors, events, versioned backend contracts, `TestPlatformBackend`, `RecordingRenderBackend`, and the headless frame lifecycle are in `NGIN.UI`. |
| 1 — Native window and renderer | Complete | `NGIN.UI.Backend.SDL3` owns SDL 3.4.12 initialization, native windows and events, SDL_GPU surfaces, rendering, resize, wait/wake, and shutdown. The standalone gallery smoke run exits successfully. |
| 2 — Composition and runtime tree | Complete | The packed generational node pool, explicit composer, static/keyed reconciliation, cleanup, retained state, and scoped invalidation have focused tests. |
| 3 — Layout | Complete | Measure/arrange, row, column, overlay, padding, alignment, scrolling, DPI conversion, display-list lowering, and debugging overlays have structural tests. |
| 4 — Text | Complete for the specified initial scope | Pinned FreeType 2.14.3, HarfBuzz 14.2.1, and OFL-licensed Noto Sans provide UTF-8 shaping, measurement, glyph rasterization, an R8 atlas, clipping, and retained `Text` rendering. |
| 5 — Input and button | Complete | Routed pointer input, hit testing, capture, hover, press, focus, keyboard activation, and semantic buttons are deterministic under the headless backend and exercised by the native gallery. |
| 6 — State and text field | Complete | Typed state and bindings, grapheme-indexed transactional editing, selection, caret, clipboard, validation, password privacy, and IME sessions have focused tests and shaped presentation. |
| 7 — Application foundations | Complete | Typed themes/resources, scroll views, multiple windows, modal dialogs, popups, independent semantics, frame diagnostics, immutable inspector snapshots, and optional visual overlays use public APIs. |
| 8 — NGIN.Core hosting | Complete | `IHostRunLoop`, `NGIN.UI.Hosting`, the UI dispatcher and registered runtime services run the shared gallery view standalone and through `NGIN.Core`. |

## Architectural Success

All success conditions from section 43 of the proposal are met:

- `NGIN.UI` has no concrete platform or graphics dependency in its public
  boundary; SDL3/SDL_GPU and `NGIN.Core` integration remain separate packages.
- Composition, reconciliation, layout, input, text presentation, semantics,
  and display-list behavior run through deterministic headless backends.
- Platform and renderer responsibilities are separate, versioned contracts
  with capability negotiation and startup validation.
- Application models own domain state while generational runtime nodes retain
  focus, editing, scrolling, and interaction state through recomposition.
- The platform event wait owns idle sleeping and can be woken by dispatched UI
  work or a host stop request.
- The standalone and hosted products compile the same `ComposeMainView()`
  implementation without private API access.
- Text reaches the renderer as HarfBuzz-shaped glyph runs rather than
  codepoint-by-codepoint drawing.
- The semantic tree is independent of layout and render output.
- Controls, renderers, and platform backends can be extended independently
  across their public contracts.

## Verification Record

The following checks passed on Windows:

```text
NGINUITests                              68/68
UI.SDL3Backend.Contracts                  1/1
UI.Hosting.Lifecycle                      1/1
NGINCoreTests                            41/41
standalone NGIN.UI.Gallery --smoke      exit 0
hosted NGIN.UI.Gallery.Hosted --smoke   exit 0
standalone and hosted V4 validation      passed
```

The normal NGIN-generated Clang/Ninja path and the direct package tests were
used. The hosted smoke run exercised service registration, presentation-stage
startup, UI-thread dispatch, rendering, stop propagation, and clean shutdown.

## Deliberately Deferred

The following proposal areas are outside the accepted version 0.1 milestone
scope:

- font fallback, multiline wrapping, full Unicode line breaking, variable
  fonts, color emoji, and bounded glyph-atlas eviction;
- platform accessibility bridges (the backend-neutral semantic model exists);
- a software pixel-snapshot renderer;
- async image decoding and device-loss resource resurrection;
- native-view embedding, component-scoped async task cancellation, render
  threading, and an animation authoring system;
- the proposal's explicit version 0.1 non-goals such as markup, designer/hot
  reload, mobile, rich text, docking, data-grid virtualization, and remote
  rendering.

These omissions do not introduce compatibility fallbacks or platform code into
the backend-neutral core.
