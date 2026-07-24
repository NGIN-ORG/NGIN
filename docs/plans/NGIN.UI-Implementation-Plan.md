# NGIN.UI Implementation Plan

Status: Active implementation plan
Source proposal: [`../proposals/NGIN.UI-Proposal.md`](../proposals/NGIN.UI-Proposal.md)
Target language: C++23

## Implementation Status

- Slice 0 is complete.
- Slice 2 is complete.
- Slice 3 is complete for the proposal's initial layout and solid-rectangle
  rendering scope, including every currently declared display-list command.
- Slice 5 is complete for the backend-neutral headless scope. Its SDL smoke
  criterion remains gated on Slice 1.
- Slice 6 state/binding foundations and keyboard/text/IME event routing are
  complete. Grapheme-aware editing and `TextField` remain pending the text
  layout cluster service.
- Slice 7 theme/resource scopes, semantic tree, multiple-window support, and
  diagnostics snapshots are complete. Retained scroll views, clipped content,
  wheel scroll chaining, platform-owned dialog foundations, and in-window
  popup foundations are complete. An inspector UI remains pending.
- Slice 1 is dependency-gated; no SDL3 dependency has been introduced.
- Slices 4, 6, 7, and 8 remain planned.

## Product Boundary

`NGIN.UI` is a standalone, backend-neutral package above `NGIN.Base`.
Concrete platform, renderer, and hosted-runtime integrations remain separate:

```text
NGIN.Base
    ↑
NGIN.UI
    ↑
NGIN.UI.Backend.SDL3

NGIN.UI + NGIN.Core
          ↑
    NGIN.UI.Hosting
```

The core package must not include SDL, native-windowing, graphics-API, or
`NGIN.Core` headers.

## Repository Decisions

- Source-owned UI code lives in `Packages/NGIN.UI/`, matching `NGIN.Core`.
- The package manifest uses the V4 package contract and
  `Build Mode="AddSubdirectory"`.
- The public boundary uses `NGIN::Utilities::Expected` and
  `NGIN::Text::String` from `NGIN.Base`.
- Platform and renderer interfaces remain separate even when one backend
  package implements both.
- Tests are structural and headless first. Native smoke tests supplement them
  after an SDL3 provider is available.
- The ergonomic value-returning view syntax is not frozen until reconciliation,
  layout, and input have exercised the explicit composer.

## Delivery Slices

### Slice 0 — Contracts and deterministic harness

Deliver:

- UI errors and results;
- device-independent and pixel geometry;
- opaque generational backend handles;
- normalized platform events;
- platform and renderer backend contracts;
- deterministic `TestPlatformBackend`;
- deep-recording `RecordingRenderBackend`;
- application/window lifecycle;
- empty-frame render/present flow;
- focused contract and lifecycle tests.

Exit criterion:

A logical window is created headlessly, receives injected resize and pointer
events, renders and presents an empty frame, then closes with both platform and
renderer resources released.

### Slice 1 — SDL3 native surface

Deliver:

- `NGIN.UI.Backend.SDL3` package;
- SDL3 initialization and native windows;
- normalized event translation;
- SDL_GPU device/surface lifecycle;
- solid-color render packet;
- resize, wait/wake, and clean shutdown;
- native smoke example.

Dependency gate:

The workspace needs explicit SDL3 package/provider and license decisions before
this slice can be built portably. No SDL2 compatibility path will be added.

### Slice 2 — Runtime tree and explicit composer

Deliver:

- packed generational node pool;
- explicit `Composer`;
- static and keyed reconciliation;
- mount/unmount cleanup;
- retained local interaction state;
- independent composition invalidation;
- reconciliation tests.

### Slice 3 — Layout and display list

Deliver:

- measure/arrange;
- `Column`, `Row`, `Overlay`, padding, spacer, and alignment;
- constraints including infinite maxima;
- DPI conversion;
- display-list commands;
- rectangle tessellation and batching;
- layout and display-list tests.

### Slice 4 — Text

Deliver:

- font-provider, shaper, and paragraph-layout contracts;
- one bundled font;
- HarfBuzz shaping and FreeType rasterization;
- glyph atlas and glyph-run display commands;
- UTF-8 single-line `Text`;
- text measurement and clipping tests.

Dependency gate:

HarfBuzz, FreeType, bundled-font licensing, and package-provider choices must be
recorded before integration.

### Slice 5 — Input and button

Deliver:

- hit testing and capture/target/bubble routing;
- pointer capture;
- hover, press, click, and focus basics;
- semantic `Button`;
- deterministic interaction tests.

### Slice 6 — State and text field

Deliver:

- `State<T>` and typed `Binding<T>`;
- keyboard routing separate from text input;
- editing buffer, caret, selection, clipboard, and validation;
- semantic `TextField`;
- invalidation-scope tests.

### Slice 7 — Application foundations

Deliver:

- typed theme and resource scopes;
- scroll view;
- multiple windows;
- popup/dialog foundations;
- semantic tree and diagnostics.

### Slice 8 — Hosted integration

Deliver:

- generic `NGIN.Core` host run-loop extension;
- `NGIN.UI.Hosting`;
- service registration and dispatcher bridge;
- standalone and hosted gallery variants.

## Verification Strategy

Each slice adds focused tests and uses the cheapest matching verification:

1. configure/build `Packages/NGIN.UI` directly;
2. run `NGINUITests`;
3. validate the V4 package through the CLI;
4. add native smoke checks only for backend slices;
5. broaden to workspace tests only when shared package/composition behavior
   changes.

## Deferred From Version 0.1

The proposal's explicit non-goals remain deferred: broad graphics/windowing
ecosystems, CSS/XAML, native controls by default, designer/hot reload, stable
plugin widget ABI, mobile, rich text, docking, data-grid virtualization,
advanced animation authoring, and remote rendering.
