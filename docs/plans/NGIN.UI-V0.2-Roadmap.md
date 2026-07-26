# NGIN.UI Version 0.2 Roadmap

Status: In progress — Milestones 17–18 complete
Theme: Production desktop foundation
Baseline: [`NGIN.UI-Post-V0.1-Roadmap.md`](NGIN.UI-Post-V0.1-Roadmap.md)
Execution map:
[`NGIN.UI-V0.2-Implementation-Workstreams.md`](NGIN.UI-V0.2-Implementation-Workstreams.md)

## Purpose

Version 0.1 proved the architecture and delivered a broad public control
surface. Version 0.2 should make that surface dependable for real desktop
applications.

The release is ordered around user-visible risk:

1. text and resource failures must be bounded and recoverable;
2. shipped examples must render the scripts they claim to demonstrate;
3. semantic controls must reach native accessibility clients;
4. common desktop layouts must not require custom measurement code;
5. large collections must scale without mounting every item;
6. ordinary control motion must be simple, bounded, and respectful of reduced
   motion without application-owned frame loops;
7. the gallery, documentation, packaging, and diagnostics must explain the
   supported product honestly.

This is a roadmap, not a compatibility specification. Public contract changes
remain governed by
[`ngin-ui-source-compatibility.md`](../policies/ngin-ui-source-compatibility.md).

## Release Principles

- Correctness and bounded resource use come before new controls.
- Backend-neutral behavior remains in `NGIN.UI`; operating-system and renderer
  integrations remain in provider packages.
- Headless structural tests remain the first verification layer. Native and
  assistive-technology tests supplement them where a platform boundary matters.
- Public controls must be keyboard-operable, semantic, theme-driven, and
  demonstrated in the gallery.
- Layout and collection features must preserve keyed retained identity.
- No dependency, font asset, or manifest/schema expansion is assumed by this
  plan. Each requires its own approval before implementation.
- Do not add silent legacy or platform fallbacks that claim support without
  providing the required behavior.

## Current Gaps

| Area | Current state | Version 0.2 target |
|---|---|---|
| Glyph storage | One fixed atlas can become full | Bounded paging or eviction with recovery |
| Font coverage | Bundled Noto Sans; optional authored fallbacks | Gallery scripts render through a packaged, licensed fallback policy |
| Emoji | Grapheme and shaping paths exist; color rendering does not | Explicit supported monochrome behavior or an approved color-emoji design |
| Images | Logical images and pluggable decoders; built-in PPM only | A practical approved PNG/JPEG path |
| Accessibility | Backend-neutral semantic tree | Windows UI Automation bridge with events and actions |
| Layout | Row, column, overlay, padding, border, scroll | Grid and wrap layout using public composition |
| Collections | Functional non-virtualized `ListView` | Reusable virtualization and incremental loading |
| Motion | Theme durations but no animation runtime or public transition API | Target-value animation, common control transitions, reduced motion, and deterministic scheduling |
| Desktop services | Clipboard, IME, windows, dialogs, file-drop events | Clear capability behavior and better native workflow diagnostics |
| Gallery | Broad catalogue | Honest international, accessibility, scale, and performance demonstrations |

## Milestone 17 — Text And Renderer Reliability

Completed 2026-07-26. Implementation and verification evidence is recorded in
[`NGIN.UI-V0.2-Progress.md`](NGIN.UI-V0.2-Progress.md).

Deliver:

- replace the single failure-prone glyph atlas with bounded atlas pages,
  eviction, or another explicitly budgeted strategy;
- define live-entry, invalidation, and redraw behavior when atlas storage is
  recycled;
- retain nearest-filtered, physical-pixel-aligned FreeType glyph rendering;
- keep image sampling independently linear;
- add diagnostics for page count, occupancy, allocation failures, eviction,
  rebuilds, and per-scale use;
- test repeated font sizes, DPI transitions, multiple windows, fallback faces,
  and long-running text churn;
- add tolerant native captures for descenders, centered text, wrapping, and
  clipping at 100%, 125%, 150%, and 200% scale;
- ensure device loss and renderer recreation rebuild text resources without
  stale handles;
- make staging failures caused by a running executable identify the locked
  destination and recommended action. This item belongs to the CLI ownership
  boundary even though the gallery exposes it.

Exit criterion:

A bounded stress test can cycle representative text, scales, and windows
without `The glyph atlas is full`, stale handles, clipped edges, or
backend-dependent filtering.

## Milestone 18 — International Text And Practical Images

Completed 2026-07-27. Implementation and verification evidence is recorded in
[`NGIN.UI-V0.2-Progress.md`](NGIN.UI-V0.2-Progress.md).

Deliver:

- define the scripts the bundled gallery promises to render;
- choose an ordered, packaged fallback-font set for those scripts;
- show the resolved fallback face and missing-glyph count in diagnostics;
- exercise Latin, Greek, Cyrillic, Arabic, combining sequences, bidi text, and
  symbols with real glyph coverage;
- decide whether version 0.2 supports monochrome emoji, color emoji, or an
  explicit unsupported placeholder;
- define and implement a practical PNG/JPEG decoder path without moving codec
  policy into the renderer backend;
- preserve asynchronous decode, cancellation, logical-resource caching, and
  device recreation;
- stage every approved font, codec artifact, license, and notice through
  authored package metadata.

Approval gates:

- Additional bundled fonts are new third-party assets and require explicit
  approval of the exact files, source revision, size, and licenses.
- A PNG/JPEG implementation that adds a library requires explicit dependency
  approval. The decision must compare a small cross-platform codec package,
  application-supplied decoders, and platform-specific providers.
- Color emoji may require new raster formats, compositing behavior, and font
  assets. It is a separate design and approval decision.

Exit criterion:

Every script named on the Typography page renders with licensed staged assets,
ordinary PNG/JPEG application images have a documented supported path, and
missing coverage is reported rather than silently presented as a successful
shaping example.

## Milestone 19 — Windows Accessibility Bridge

Deliver:

- define a platform accessibility-provider boundary that consumes the existing
  semantic tree without exposing runtime nodes;
- implement a Windows UI Automation provider in an operating-system-specific
  package;
- map roles, names, descriptions, values, ranges, checked/selected/expanded
  state, enabled/focus state, bounds, and supported actions;
- diff semantic snapshots and publish focus, property, structure, selection,
  and live-region events;
- route accessibility actions back to the UI thread and the owning element;
- handle popup, dialog, multiple-window, virtualized-item, password, and
  destroyed-element lifetimes safely;
- add provider contract tests and a documented manual screen-reader checklist;
- expose accessibility capability and failure diagnostics without pretending
  unsupported platforms have a native bridge.

Exit criterion:

The gallery can be navigated and operated with Windows Narrator using names,
roles, state, focus, values, and actions sourced from the public semantic tree.

macOS Accessibility and Linux AT-SPI bridges remain follow-up provider
milestones. They must reuse the provider-neutral semantic projection rather
than change control composition per platform.

## Milestone 20 — Desktop Layout Primitives

Deliver:

- `Grid` with fixed, automatic, and weighted tracks;
- row/column placement and spans;
- deterministic intrinsic measurement under finite and infinite constraints;
- `WrapPanel` with configurable orientation, gaps, and line alignment;
- a deliberately bounded `Canvas`/absolute-placement primitive for diagrams,
  overlays, and custom surfaces;
- responsive gallery layouts that remain usable at narrow window sizes;
- diagnostics that expose resolved grid tracks and wrapped lines;
- tests for DPI, nested scrolling, minimum/maximum sizes, flex interaction,
  visibility, clipping, and keyed reconciliation.

Exit criterion:

A settings form, responsive toolbar, and dashboard tile layout can be composed
with public layout primitives and no custom measure/arrange implementation.

## Milestone 21 — Virtualized Collections

Deliver:

- a reusable viewport virtualization engine with overscan;
- stable mapping between item keys, source indices, realized nodes, selection,
  focus, and semantic identity;
- variable-size item support or an explicit fixed/estimated-size version 0.2
  contract;
- integration with `IIncrementalDataSource`;
- scroll anchoring through insert, remove, reorder, filtering, and async range
  arrival;
- keyboard navigation, type-ahead, selection, ensure-visible, and accessibility
  behavior across unrealized items;
- `ListView` adoption without breaking ordinary in-memory lists;
- performance and allocation budgets for at least 100,000 logical items;
- a gallery page that demonstrates realization counts and incremental loading.

Exit criterion:

A 100,000-item logical list keeps realized element count proportional to the
viewport while preserving selection, focus, scrolling, semantics, and keyed
identity.

`TreeView` may build on this engine if it does not delay the base virtualized
list. `DataGrid` is planned after the virtualization and grid-layout contracts
have proven stable.

## Milestone 22 — Motion Foundations

Deliver:

- define a small target-value animation API that fits repeatable composition
  and is as simple to use as ordinary state changes;
- retain animation progress by stable element identity and safely retarget or
  cancel animation when composition changes;
- interpolate scalar values, colors, opacity, translation, and scale with a
  small set of standard easing curves;
- schedule frames from monotonic deadlines only while animation is active,
  without requiring application timers or permanent redraw loops;
- define whether animated transforms affect only painting or also clipping,
  hit testing, semantics, and layout, and keep that behavior consistent;
- add automatic theme-driven transitions for common hover, press, focus,
  disabled, popup, and progress-indicator states;
- expose a move-safe animation handle for explicit cancellation and bounded
  repeating animations;
- obtain reduced-motion preference through the platform capability boundary
  and settle animations immediately when motion is disabled;
- provide a deterministic test clock and tests for retargeting, cancellation,
  unmounting, multiple windows, frame deadlines, and reduced motion;
- add a Gallery Motion page that demonstrates transitions, interruption,
  repetition, easing, and reduced-motion behavior using public APIs.

This milestone is deliberately not a general timeline or keyframe framework.
Rotation, arbitrary layout animation, shared-element transitions, and advanced
sequence authoring remain follow-up work.

Exit criterion:

A developer can add a fade, translation, color change, or control-state
transition without writing a frame loop; animations stop requesting frames
when idle, remain safe when elements disappear, and become immediate when
reduced motion is enabled.

## Milestone 23 — Version 0.2 Product Completion

Deliver:

- update the gallery with text coverage, accessibility, responsive layout,
  virtualization, motion, and resource diagnostics pages;
- add searchable gallery navigation and concise, copyable public examples;
- keep standalone, hosted, and headless products on the same view model;
- update guides and generated API comments for every new public contract;
- add migration notes for any accepted breaking or deprecated API;
- run native smoke checks on supported platforms and Windows accessibility
  checks;
- verify package restore, build, stage, launch, install/export, licenses, and
  runtime notices from a clean consumer;
- publish performance, memory, atlas, image-cache, and realization budgets;
- produce a versioned demo build that a developer can run without the source
  tree.

Exit criterion:

The version 0.2 release can be built and consumed from authored NGIN manifests,
passes its documented quality gates, and demonstrates every supported feature
without known placeholder output.

## Version 0.2 Definition Of Done

Version 0.2 is complete only when:

- glyph storage is bounded and recoverable;
- gallery-promised scripts have real staged glyph coverage;
- missing glyphs and resource pressure are diagnosable;
- Windows UI Automation exposes and operates the semantic control tree;
- Grid and WrapPanel are public, tested, documented, and demonstrated;
- virtualized lists scale to the published logical-item budget;
- target-value animations and control transitions are public, deterministic,
  cancellation-safe, and reduced-motion aware;
- standalone, hosted, and headless gallery paths pass;
- dependency licenses and runtime notices are complete;
- developer documentation and examples match the shipped behavior.

## Explicitly Outside Version 0.2

- rich-text editing and document layout;
- a full `DataGrid`;
- docking and document-window systems;
- markup or declarative UI languages;
- designer and hot reload;
- mobile backends;
- remote rendering;
- a stable binary plugin-widget ABI;
- a general animation authoring framework, including arbitrary keyframes,
  shared-element transitions, and automatic layout transitions;
- native-view embedding.

These may receive separate plans after the version 0.2 foundations are proven.

## Delivery Rule

Each milestone closes with:

1. focused implementation and tests;
2. the matching gallery and documentation changes;
3. one targeted verification pass;
4. an update to the roadmap/progress evidence;
5. a committed milestone boundary before work begins on the next milestone.
