# NGIN.UI Implementation Plan

Status: Implemented for the version 0.1 milestone scope
Source proposal: [`../proposals/NGIN.UI-Proposal.md`](../proposals/NGIN.UI-Proposal.md)
Target language: C++23
Acceptance audit: [`NGIN.UI-V0.1-Acceptance-Audit.md`](NGIN.UI-V0.1-Acceptance-Audit.md)

## Implementation Status

- Slice 0 is complete, including backend contract versioning, capability
  negotiation, and startup validation.
- Slice 2 is complete.
- Slice 3 is complete for the proposal's initial layout and solid-rectangle
  rendering scope, including every currently declared display-list command.
- Slice 4 has dependency-free font-provider, shaping, paragraph-layout, and
  grapheme-segmentation contracts plus atlas-backed glyph-run display commands
  and renderer lowering. An injectable glyph-atlas contract and semantic
  `Text` element now provide constraint-aware measurement, clipping, and
  DPI-aware shaped-glyph painting. The concrete `NativeTextSystem` now owns
  pinned Noto Sans/FreeType/HarfBuzz services, extended grapheme segmentation,
  single-line paragraph/range geometry, and a DPI-keyed R8 glyph atlas.
- Slice 5 is complete for the backend-neutral headless scope. Its SDL smoke
  criterion remains gated on Slice 1.
- Slice 6 state/binding foundations, keyboard/text/IME event routing, and the
  transactional grapheme-indexed UTF-8 editing buffer are complete. The
  retained semantic `TextField` supports binding validation, keyboard/text
  editing, clipboard commands, transient IME composition, platform text-input
  lifecycle, and password-value privacy. It now shares the shaped-text pipeline
  and paints injected bidi-safe selection, composition, and caret geometry.
- Slice 7 is complete for the backend-neutral scope: theme/resource scopes,
  semantic trees, multiple windows, retained scrolling, platform-owned dialog
  foundations, in-window popups, frame timings, inspector snapshots, and
  debugging overlays. A text-rendered inspector panel remains coupled to
  Slice 4.
- Slice 1 now has pinned SDL 3.4.12 source-provider and
  `NGIN.UI.Backend.SDL3` packages. The backend implements SDL3 initialization,
  windows, normalized events, wait/wake, system services, and SDL_GPU-backed
  multi-window rendering. `Examples/NGIN.UI.Gallery` is the standalone native
  smoke product and exercises retained controls and shaped text through that
  backend.
- Slice 8 is complete. The generic `NGIN.Core::IHostRunLoop` extension provides
  builder injection, stop-state observation, and wake-on-stop behavior.
  `NGIN.UI.Hosting` owns the UI application, native text services, UI-thread
  dispatcher, and event-driven host run loop. The standalone and hosted gallery
  products share one retained view; both pass native SDL smoke runs.
- The remaining Slice 4 text hardening is planned beyond the initial
  single-line text scope.

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
- Approved third-party sources use pinned upstream archives with SHA-256
  verification. FreeType 2.14.3 and HarfBuzz 14.2.1 link privately into the
  native text implementation; Noto Sans is pinned from `google/fonts` under
  OFL-1.1. SDL 3.4.12 is reserved for the backend package.
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

SDL3 and SDL_GPU integration was approved on 2026-07-24. The implementation
pins SDL 3.4.12 at commit
`f87239e71e42da91ca317a12eefb82cfbf3393eb`, verifies its source archive with
SHA-256, and retains the zlib license in the source-provider package. No SDL2
compatibility path is included.

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

Current status:

The backend-neutral font, shaping, paragraph-layout, grapheme-segmentation, and
glyph-atlas contracts are implemented, as is the atlas-backed glyph-run
display command and its renderer lowering. The semantic `Text` element measures
through an injected paragraph service and paints resolved atlas glyphs with
clipping and DPI-aware requests. `TextField` reuses the same pipeline and
injected bidi-safe geometry for selection, IME composition underlines, and the
caret. Password presentation shapes only a grapheme-count mask. The concrete
`NativeTextSystem` uses the bundled Noto Sans face, FreeType metrics and
rasterization, HarfBuzz shaping, extended grapheme segmentation, single-line
paragraph/range geometry, and a renderer-backed DPI-keyed R8 atlas. Font
fallback, full UAX #29 segmentation, multiline wrapping, and atlas eviction are
later general-purpose hardening.

Deliver:

- font-provider, shaper, and paragraph-layout contracts;
- one bundled font;
- HarfBuzz shaping and FreeType rasterization;
- glyph atlas and glyph-run display commands;
- UTF-8 single-line `Text`;
- text measurement and clipping tests.

Dependency gate:

HarfBuzz, FreeType, and a bundled OFL-licensed font were approved on
2026-07-24. Their selected providers and exact license metadata must be recorded
during integration.

### Slice 5 — Input and button

Deliver:

- hit testing and capture/target/bubble routing;
- pointer capture;
- hover, press, click, and focus basics;
- semantic `Button`;
- deterministic interaction tests.

### Slice 6 — State and text field

Current status:

State, bindings, routed keyboard/text/IME events, and the transactional
grapheme-indexed UTF-8 editing buffer are implemented. The editing buffer
rejects malformed UTF-8 and invalid segmentation without mutating its prior
state. The retained semantic `TextField` integrates binding validation,
keyboard/text editing, clipboard shortcuts, selection retention, and
password-value privacy. Transient IME composition is isolated from application
state until commit, validation failure restores the editing session, and focus
coordinates platform text-input start/stop with a DPI-aware candidate
rectangle. Visual presentation reuses injected shaped-text services and
bidi-safe geometry to paint selection, IME composition, and the caret; password
fields shape only a grapheme-count mask.

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
