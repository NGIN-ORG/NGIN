# NGIN.UI

`NGIN.UI` is the backend-neutral core of NGIN's C++23 application UI toolkit.
Its public contracts depend only on `NGIN.Base`; its native text implementation
privately links pinned FreeType and HarfBuzz sources. Native windowing, graphics
APIs, and optional `NGIN.Core` hosting belong to separate packages.

The version 0.1 implementation provides:

- device-independent and pixel geometry;
- generational platform/render handles;
- structured UI errors and results;
- normalized window, pointer, keyboard, text, drop, and theme events;
- versioned platform and renderer backend contracts with explicit capability
  negotiation and startup validation;
- application and logical-window lifecycle;
- deterministic headless platform and recording renderer implementations;
- explicit RAII-scoped composition;
- packed generational runtime-node storage;
- static and keyed reconciliation;
- independent composition, layout, and paint invalidation;
- constraint-based measure/arrange for rows, columns, overlays, padding,
  alignment, and opt-in main-axis flex growth/shrinkage;
- typed node layout and solid-background properties;
- backend-neutral display-list construction;
- DPI-aware rectangle, rounded-rectangle, stroke, and image tessellation;
- nested transform, clip, and opacity stacks with texture-aware batch
  coalescing;
- backend-neutral font-provider, shaping, paragraph-layout, and grapheme
  segmentation contracts;
- a concrete `NativeTextSystem` combining Noto Sans, FreeType metrics and
  rasterization, HarfBuzz shaping, extended grapheme segmentation,
  paragraph/range geometry, and a renderer-backed R8 glyph atlas;
- an injectable glyph-atlas contract and semantic `Text` element with
  constraint-aware measurement, clipping, and DPI-aware shaped-glyph painting;
- atlas-backed glyph-run display commands with DPI-aware renderer lowering;
- reverse-paint-order hit testing and capture/target/bubble pointer routing;
- retained hover, pressed, pointer-capture, and keyboard-focus state;
- normalized routed keyboard/text events and explicit tab traversal;
- keyboard-accessible button activation with focus-safe pressed state;
- semantic composer buttons with deterministic activation;
- retained scroll views with unbounded-axis measurement, clipped content,
  wheel scrolling, and ancestor scroll chaining;
- window-level `DialogWindow` ownership with modal input blocking, focus
  restoration, and cascading close;
- in-window popup overlays with viewport-aware placement, top-layer painting,
  modal focus scopes, and outside-pointer or Escape dismissal;
- observable `State<T>` with scoped invalidation scheduling;
- move-only RAII subscriptions and typed, validatable `Binding<T>` adapters;
- transactional UTF-8 editing with grapheme-indexed caret, selection,
  replacement, and deletion operations;
- retained semantic `TextField` sessions with binding validation, routed
  keyboard/text input, clipboard shortcuts, transient IME composition, and
  password-value privacy;
- shaped `TextField` presentation with injected bidi-safe selection,
  composition, and caret geometry plus grapheme-count password masking;
- platform IME start/stop coordination with DPI-aware candidate rectangles;
- immutable/revisioned theme values and hierarchical typed resource scopes;
- typed visual-state styles, theme-driven borders and rounded backgrounds,
  visible focus treatment, and border/separator composition helpers;
- a render-tree-independent semantic tree with stable element ownership;
- per-window structural diagnostics for composition, layout, rendering, focus,
  pointer capture, and frame-phase timings;
- immutable inspector snapshots of runtime and semantic trees plus optional
  layout, hit-test, focus, and selection overlays.

Build and test directly:

```bash
cmake -S Packages/NGIN.UI -B build/ngin-ui -DNGIN_UI_BUILD_TESTS=ON
cmake --build build/ngin-ui --target NGINUITests
ctest --test-dir build/ngin-ui --output-on-failure
```

The package intentionally has no SDL dependency. Native windows and rendering
are supplied by the separate
[`NGIN.UI.Backend.SDL3`](../NGIN.UI.Backend.SDL3/) package. Applications can
run the same view directly or through the optional
[`NGIN.UI.Hosting`](../NGIN.UI.Hosting/) bridge to `NGIN.Core`.

See the [styling guide](../../docs/guides/ngin-ui-styling.md) for theme tokens,
state precedence, focus treatment, borders, and invalidation rules.

The default build fetches the pinned text dependencies recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Configure with
`-DNGIN_UI_FETCH_THIRD_PARTY=OFF` to require installed FreeType 2.14 and
HarfBuzz 14 packages instead, or `-DNGIN_UI_ENABLE_NATIVE_TEXT=OFF` for the
contracts-only core. The render backend passed to `NativeTextSystem::Create`
must be initialized and must outlive the text system. Installed or staged
applications may pass an explicit font path in `NativeTextCreateInfo`; the
empty-path default is the authored font in the source package.
