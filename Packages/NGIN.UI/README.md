# NGIN.UI

Current package version: `0.4.0`. See the
[0.4 release notes](../../docs/guides/ngin-ui-v0.4-release.md) for migration,
published resource budgets, and the versioned Gallery archive.

`NGIN.UI` is the backend-neutral core of NGIN's C++23 application UI toolkit.
Its public contracts depend only on `NGIN.Base`; its native text and standard
image implementations privately compile pinned FreeType, HarfBuzz, and
stb_image sources. Native windowing, graphics APIs, and optional `NGIN.Core`
hosting belong to separate packages.

Start with the
[NGIN.UI developer documentation](../../docs/guides/ngin-ui.md) or the
[five-minute standalone window](../../docs/guides/ngin-ui-first-window.md).
The [application composition guide](../../docs/guides/ngin-ui-application-composition.md)
and `Examples/NGIN.UI.MultiPage` show services, ViewModels, pages, navigation,
async loading, and safe shutdown together.
Public source compatibility and deprecation rules are defined in the
[NGIN.UI compatibility policy](../../docs/policies/ngin-ui-source-compatibility.md).
Version 0.4 delivery evidence is recorded in the
[version 0.4 roadmap](../../docs/plans/NGIN.UI-V0.4-Roadmap.md) and its
[progress log](../../docs/plans/NGIN.UI-V0.4-Progress.md).

Version 0.4 provides:

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
- desktop `Grid` tracks and spans, responsive `WrapPanel` lines, and bounded
  absolute placement through `Canvas`, with resolved-layout diagnostics;
- typed node layout and solid-background properties;
- backend-neutral display-list construction;
- DPI-aware rectangle, rounded-rectangle, stroke, and image tessellation;
- automatic one-physical-pixel coverage anti-aliasing for solid fills and
  strokes across every renderer backend;
- nested transform, clip, and opacity stacks with texture-aware batch
  coalescing;
- retained target-value animation for scalar values, colors, opacity,
  translation, and scale with deterministic platform time, easing,
  interruption, cancellation, bounded repetition, and reduced motion;
- retained motion controllers with generic awaitable properties, UI-scheduled
  continuations, ordered `co_await` steps, and parallel `WhenAll`/`WhenAny`
  composition;
- typed synchronous and asynchronous commands with observable availability,
  execution, cancellation, bounded concurrency, errors, and window/application
  lifetime safety;
- theme-driven control-state transitions, animated popup entry/exit, and
  determinate or indeterminate progress motion without application frame
  loops;
- backend-neutral font-provider, shaping, paragraph-layout, and grapheme
  segmentation contracts;
- a concrete `NativeTextSystem` combining Noto Sans, FreeType metrics and
  rasterization, fallback-aware HarfBuzz shaping, extended grapheme
  segmentation, multiline Unicode-aware wrapping, paragraph/range geometry,
  and a renderer-backed R8 glyph atlas;
- packaged Noto Sans Arabic and Noto Sans Symbols 2 fallbacks plus loaded-face,
  fallback-use, and missing-glyph diagnostics;
- bounded, lazily allocated glyph-atlas pages with live-reference-safe
  recycling, renderer-device restoration, and capacity diagnostics;
- an injectable glyph-atlas contract and semantic `Text` element with
  constraint-aware measurement, clipping, and DPI-aware shaped-glyph painting;
- atlas-backed glyph-run display commands with DPI-aware renderer lowering;
- reverse-paint-order hit testing and capture/target/bubble pointer routing;
- retained hover, pressed, pointer-capture, and keyboard-focus state;
- normalized routed keyboard/text events and explicit tab traversal;
- keyboard-accessible button activation with focus-safe pressed state;
- semantic composer buttons with deterministic activation;
- retained scroll views with unbounded-axis measurement, clipped content,
  wheel scrolling, ancestor scroll chaining, and visual scrollbars with
  pointer dragging and keyboard movement;
- typed zero/single/multiple selection models, semantic list views and stable
  keyed list items with pointer, arrow, Home/End, type-ahead, and
  ensure-visible behavior;
- anchored combo boxes, retained-state tabs, menu buttons, and context menus;
- fixed-row `VirtualizedListView` composition with viewport overscan,
  stable-key scroll anchoring, incremental range request/cancellation,
  unrealized-item keyboard and accessibility behavior, realization
  diagnostics, and a 100,000-item time/allocation budget;
- window-level `DialogWindow` ownership with modal input blocking, focus
  restoration, and cascading close;
- in-window popup overlays with viewport-aware placement, top-layer painting,
  modal focus scopes, and outside-pointer or Escape dismissal;
- observable `State<T>` with scoped invalidation scheduling;
- move-only RAII subscriptions and typed, validatable `Binding<T>` adapters;
- lifetime-safe read-only bindings, explicitly dependent computed state, and
  nested state batching with coalesced notification;
- immediate, deferred, submit-time, and stale-safe asynchronous field
  validation with ordered form summaries and command availability binding;
- owning ViewModel task scopes, keyed plain-type lifecycle hosts, and
  observable idle/loading/content/empty/error presentation state;
- an explicit typed page catalogue that associates a stable page tag and ID
  with one ViewModel, typed navigation parameter, and synchronous View;
- window-local or named-region navigation stacks with startup, push, replace,
  back, clear, keyboard back, rollback-safe activation, stable retained page
  keys, snapshots, and bounded opt-in page caching;
- transactional UTF-8 editing with grapheme-indexed caret, selection,
  replacement, and deletion operations;
- retained semantic `TextField` sessions with binding validation, routed
  keyboard/text input, clipboard shortcuts, transient IME composition, and
  password-value privacy;
- shaped `TextField` presentation with injected bidi-safe selection,
  composition, and caret geometry plus grapheme-count password masking;
- platform IME start/stop coordination with DPI-aware candidate rectangles;
- multiline `TextArea` editing with explicit and wrapped lines, vertical caret
  navigation, retained preferred x position, and automatic caret scrolling;
- logical RGBA image resources with memory, file, and generated-pixel sources,
  pluggable asynchronous decoding and cancellation, lazy texture upload, and
  explicit device-loss/recreation hooks;
- a bounded least-recently-used image texture cache with entry, byte, peak,
  eviction, and capacity diagnostics;
- built-in bounded PNG, JPEG, and PPM decoding to RGBA8;
- semantic `Image` composition with none/fill/contain/cover/scale-down fit,
  revisioned dynamic RGBA surfaces, stable in-place texture uploads, and
  normalized image-region drawing for zoomable custom views,
  alignment, tint, and clipping;
- immutable/revisioned theme values and hierarchical typed resource scopes;
- typed visual-state styles, theme-driven borders and rounded backgrounds,
  visible focus treatment, and border/separator composition helpers;
- a supported custom-element leaf contract with retained typed local state,
  constraint measurement, arrangement, bounded local-coordinate painting,
  routed input, semantics, invalidation, and unmount cleanup;
- theme-driven checkbox, typed radio-button, toggle-switch, slider, and
  progress-bar controls with disabled, focus, validation, and semantic states;
- explicit label associations and delayed non-focus-stealing tooltips;
- a render-tree-independent semantic tree with stable element ownership;
- immutable accessibility snapshots, semantic diffs, UI-thread action routing,
  provider capability diagnostics, and password-safe values;
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

Run the release-budget benchmarks, including ordinary heap-allocation counts:

```bash
cmake -S Packages/NGIN.UI -B build/ngin-ui-benchmarks \
  -DNGIN_UI_BUILD_BENCHMARKS=ON
cmake --build build/ngin-ui-benchmarks --target NGINUIBenchmarks
./build/ngin-ui-benchmarks/benchmarks/NGINUIBenchmarks
```

Generate the browsable API reference with Doxygen:

```bash
cmake -S Packages/NGIN.UI -B build/ngin-ui-docs -DNGIN_UI_BUILD_DOCS=ON
cmake --build build/ngin-ui-docs --target NGINUIDocs
```

Open `build/ngin-ui-docs/docs/html/index.html`.

The package intentionally has no SDL dependency. Native windows and rendering
are supplied by the separate
[`NGIN.UI.Backend.SDL3`](../NGIN.UI.Backend.SDL3/) package. Applications can
run the same view directly or through the optional
[`NGIN.UI.Hosting`](../NGIN.UI.Hosting/) bridge to `NGIN.Core`.
Windows UI Automation is supplied by the optional
[`NGIN.UI.Accessibility.Windows`](../NGIN.UI.Accessibility.Windows/) package.

See the [styling guide](../../docs/guides/ngin-ui-styling.md) for theme tokens,
state precedence, focus treatment, borders, and invalidation rules.
See the
[custom-control guide](../../docs/guides/ngin-ui-custom-controls.md) for
composite controls, custom measurement/painting, retained local state, input,
semantics, lifecycle, and error rules.
See the
[MVVM architecture guide](../../docs/guides/ngin-ui-mvvm.md) for Model, View,
ViewModel, composition, state, commands, validation, and task ownership.
See the
[derived-state and validation guide](../../docs/guides/ngin-ui-state-validation.md)
for read-only bindings, computed values, batching, asynchronous validation,
form summaries, and command availability.
See the
[ViewModel lifetime guide](../../docs/guides/ngin-ui-viewmodel-lifetime.md) for
owned tasks, drain-aware closure, activation, keyed replacement, standalone
service factories, hosted DI ownership, loading states, retry, cancellation,
and shutdown rules.
See the
[foundational-controls guide](../../docs/guides/ngin-ui-foundational-controls.md)
for bindings, typed radio groups, labels, tooltips, sliders, progress, and
scrollbars.
See the
[collections and navigation guide](../../docs/guides/ngin-ui-collections-navigation.md)
for selection models, keyed and virtualized lists, incremental range loading,
combo boxes, retained tabs, and menus.
See the
[desktop layout guide](../../docs/guides/ngin-ui-desktop-layout.md) for Grid,
WrapPanel, Canvas, responsive sizing, and layout diagnostics.
See the [motion guide](../../docs/guides/ngin-ui-motion.md) for target changes,
easing, awaited sequences, parallel motion, interruption, cancellation,
repetition, transforms, reduced motion, and deterministic clocks.
See the
[richer-content guide](../../docs/guides/ngin-ui-richer-content.md) for
multiline layout, font fallback, `TextArea`, logical images, asynchronous
decoding, and device recreation.
See the
[testing and release guide](../../docs/guides/ngin-ui-testing-and-release.md)
for deterministic pixel tests, gallery smoke coverage, allocation budgets,
cache diagnostics, install/export consumption, and staged licenses.

The default build fetches the pinned text and image dependencies recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Configure with
`-DNGIN_UI_FETCH_THIRD_PARTY=OFF` to require installed FreeType 2.14 and
HarfBuzz 14 packages plus an installed `stb_image.h`. Use
`-DNGIN_UI_ENABLE_NATIVE_TEXT=OFF` or
`-DNGIN_UI_ENABLE_STANDARD_IMAGE_FORMATS=OFF` to remove either private
implementation. The render backend passed to `NativeTextSystem::Create` must
be initialized and must outlive the text system. Staged applications find the
packaged font set under `fonts/NGIN.UI`; installed applications may pass
explicit paths in `NativeTextCreateInfo`.
