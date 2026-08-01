# NGIN.UI Version 0.2 Implementation Workstreams

Status: Active execution map
Roadmap: [`NGIN.UI-V0.2-Roadmap.md`](NGIN.UI-V0.2-Roadmap.md)

## Purpose

This document turns the version 0.2 roadmap into bounded engineering
workstreams. It identifies ownership, sequencing, decisions, tests, and
acceptance gates. It does not approve dependencies or public schema changes.

## Ownership Boundaries

| Concern | Primary ownership |
|---|---|
| Backend-neutral controls, layout, text, resources, semantics | `Packages/NGIN.UI/` |
| SDL windows, events, and renderer behavior | `Packages/NGIN.UI.Backend.SDL3/` |
| Native accessibility providers | New platform-specific package after contract approval |
| Hosted lifecycle and dispatcher integration | `Packages/NGIN.UI.Hosting/` |
| Shared public demonstrations | `Examples/NGIN.UI.Gallery/` |
| Headless application coverage | `Examples/NGIN.UI.Gallery.Tests/` |
| Hosted smoke product | `Examples/NGIN.UI.Gallery.Hosted/` |
| Build/stage diagnostics | `Tools/NGIN.CLI/` |
| Public guides and plans | `docs/guides/` and `docs/plans/` |

Generated output under `build/` is verification evidence, never an
implementation surface.

## Workstream A — Glyph Storage And Text Rendering

Status: Completed in Milestone 17 on 2026-07-26.

### Goal

Make text rendering bounded, stable across DPI, and recoverable across renderer
resource lifetimes.

### Decisions Before Editing

- atlas pages versus eviction/repack;
- whether glyph handles remain UV snapshots or become indirections that survive
  recycling;
- page-size and total-memory budgets;
- eviction policy and frame-safety rule;
- per-face/per-scale partitioning;
- how device loss invalidates cached glyph entries;
- whether transformed text beyond translation and DPI scale is supported in
  version 0.2.

### Accepted Milestone 17 Design

- up to four lazily allocated 1024 by 1024 R8 pages by default;
- configurable page dimensions and page count with an explicit fixed budget;
- least-recently-used page rebuilding only after runtime and display-list
  leases have expired;
- cached UV snapshots remain valid for the lifetime of their page lease;
- scale remains part of the glyph cache key;
- device loss clears renderer handles and device restoration starts with a new
  empty page;
- a resource-invalidated callback marks every open window for full relayout;
- nearest-filtered glyph coverage is snapped in renderer lowering, while image
  textures remain linearly filtered;
- translation and DPI scale are supported; arbitrary transformed text remains
  outside this milestone.

### Implementation Shape

- keep shaping and paragraph layout independent of glyph storage;
- give cached entries an atlas generation or equivalent stale-entry guard;
- resolve or refresh recycled glyphs before display-list submission;
- retain `TextureFilter::Nearest` for target-scale FreeType coverage;
- retain physical-pixel snapping only in renderer lowering;
- expose atlas page, occupancy, eviction, rebuild, miss, and failure counters;
- provide deterministic small-budget tests that force recycling quickly.

### Focused Tests

- atlas fills, recycles, and continues rendering;
- a recycled entry cannot draw a different glyph;
- two windows and two scales can alternate without stale UVs;
- device recreation rebuilds live text;
- centered and wrapped descenders remain inside clips at common scales;
- repeated Unicode text churn remains under the declared memory budget.

### First Acceptance Gate

A test atlas intentionally smaller than the working set completes repeated
frames without allocation failure or stale glyph output.

## Workstream B — Font Coverage And Missing-Glyph Policy

Status: Completed in Milestone 18 on 2026-07-27.

### Goal

Make international-text claims explicit, licensed, staged, and observable.

### Decisions Before Editing

- exact scripts promised by the gallery and default package;
- exact fallback font files and whether they are variable or static;
- package-size budget;
- application override and fallback ordering;
- missing-glyph detection and diagnostics;
- monochrome versus color emoji policy.

### Dependency Gate

Produce an approval note listing for every proposed font:

- upstream project and immutable revision;
- exact file and checksum;
- license and notice location;
- compressed and staged size;
- scripts covered;
- why the current Noto Sans file is insufficient.

No font file is added before that approval.

### Implementation Shape

- use the existing `fallbackFontPaths` contract where sufficient;
- do not add an operating-system-global font search as a silent fallback;
- segment fallback spans without breaking grapheme clusters or bidi shaping;
- count unresolved glyphs and expose the selected face in diagnostics;
- make the gallery derive its claims from the configured packaged set.

### First Acceptance Gate

The Typography page renders its declared Latin, Greek, Cyrillic, Arabic,
combining, bidi, and symbol samples without unexpected `.notdef` glyphs.

## Workstream C — Practical Image Codecs

Status: Completed in Milestone 18 on 2026-07-27.

### Goal

Provide an ordinary application image path while preserving logical resources
and backend independence.

### Decisions Before Editing

- core built-in decoder, optional codec package, or platform codec providers;
- PNG-only first slice versus PNG and JPEG together;
- color-space and alpha conversion contract;
- animated-image non-goal;
- decode limits for dimensions, bytes, and malformed input;
- dependency and license choice.

### Dependency Gate

Before implementation, compare candidates using:

- supported formats;
- cross-platform determinism;
- security and maintenance model;
- binary and source size;
- license;
- CMake/package integration;
- whether decode can remain outside renderer backends.

The selected dependency or platform-provider approach requires explicit
approval.

### Focused Tests

- known PNG/JPEG pixels and alpha;
- malformed, truncated, oversized, and cancelled decode;
- async completion after the requesting element unmounts;
- cache hit, eviction, device loss, and restoration;
- installed/staged consumer loading.

### First Acceptance Gate

A staged gallery build loads a normal authored image through the public
`ImageResource` path with no gallery-private decoder.

## Workstream D — Accessibility Provider Contract And UI Automation

### Goal

Project the existing semantic tree into a native Windows accessibility tree
without coupling controls to UI Automation.

### Contract Slice

- semantic snapshot identity and revision;
- provider root per native window;
- node lookup by stable semantic/element identity;
- native bounds conversion;
- pattern/action dispatch onto the UI thread;
- semantic change notifications;
- lifetime and stale-provider behavior;
- capability and diagnostic reporting.

### Windows Provider Slice

- UI Automation fragments and roots;
- control type and property mapping;
- Invoke, Toggle, Selection, SelectionItem, RangeValue, Value, ExpandCollapse,
  and Scroll patterns where supported;
- focus, structure, property, selection, and live-region events;
- dialog, popup, password, and multiple-window behavior.

### Focused Tests

- semantic snapshot diffs emit the expected provider events;
- native queries never dereference destroyed runtime elements;
- accessibility actions follow the normal control path;
- password values remain private;
- focus and popup ownership match keyboard behavior;
- virtualized items report collection position and realization consistently.

### First Acceptance Gate

An automated provider test and a manual Narrator pass can identify, navigate,
read, and operate the gallery's primary controls.

## Workstream E — Grid, Wrap, And Canvas Layout

### Goal

Add common desktop layouts without weakening the existing constraint model.

### Grid Slice

- authored fixed, auto, and weighted track definitions;
- placement and spans;
- intrinsic measurement;
- deterministic deficit and surplus distribution;
- minimum/maximum track constraints;
- diagnostics for resolved tracks.

### Wrap Slice

- horizontal and vertical orientation;
- gaps, line gaps, and line alignment;
- stable child order and keyed identity;
- behavior under infinite main-axis constraints.

### Canvas Slice

- explicit child offsets;
- clear desired-size contribution rules;
- inherited clip, transform, opacity, input, and semantics;
- no implicit replacement for ordinary responsive layout.

### Focused Tests

- finite/infinite constraints and nested grids;
- spans with fixed/auto/weighted tracks;
- narrow-window wrapping;
- scroll and visibility interaction;
- DPI and fractional logical sizes;
- deterministic reconciliation after track or child changes.

### First Acceptance Gate

The gallery's settings and dashboard examples use only public Grid/Wrap APIs
and remain usable at the supported minimum window size.

## Workstream F — Collection Virtualization

### Goal

Keep realized UI work proportional to the viewport rather than source size.

### Contract Slice

- logical item count and stable item key;
- viewport and overscan range;
- estimated or measured extent model;
- realized range diagnostics;
- range requests and cancellation;
- scroll anchoring;
- focus/selection representation for unrealized items;
- semantic collection metadata.

### Implementation Slice

- reusable virtualizing layout state;
- `ListView` adapter;
- incremental-source integration;
- item recycling only when it preserves retained identity rules;
- ensure-visible across unrealized ranges;
- variable-height follow-up only if the initial contract can support it
  correctly.

### Performance Gates

- 100,000 logical items;
- realized nodes bounded by viewport plus overscan;
- no full-source composition, measurement, or semantic-node allocation;
- published frame-time and allocation budgets;
- stable scrolling after insert/remove before the viewport.

### First Acceptance Gate

The headless and native gallery examples navigate a 100,000-item source while
diagnostics show a bounded realized range.

## Workstream G — Gallery, Diagnostics, And Documentation

### Goal

Make every version 0.2 behavior discoverable and independently verifiable.

### Scope

- Typography coverage and missing-glyph diagnostics;
- atlas pressure/recycling demonstration;
- Accessibility page with semantic/provider status;
- responsive Grid and Wrap examples;
- virtualized collection with realized-range diagnostics;
- practical image decode example;
- searchable navigation and concise code references;
- standalone, hosted, and headless parity;
- updated first-window, content, collections, accessibility, backend, testing,
  troubleshooting, and API documentation.

### First Acceptance Gate

The gallery smoke model visits every version 0.2 page and each page has focused
headless behavior coverage.

## Workstream H — Build, Stage, And Release Workflow

### Goal

Make development and consumption failures actionable.

### Scope

- detect and explain a staged executable locked by a running process;
- preserve the successful build artifact when staging fails;
- show source and destination paths plus the recommended close/retry action;
- verify font, image, provider, license, and runtime-notice staging;
- validate standalone and hosted manifests;
- verify install/export consumption;
- produce a versioned runnable gallery package.

This workstream crosses into `Tools/NGIN.CLI/`; changes must follow the active
V4 graph and staging contracts rather than adding a UI-specific build path.

### First Acceptance Gate

Rebuilding a running gallery fails with a specific locked-artifact diagnostic,
and rebuilding after closing it succeeds without deleting the build tree.

## Workstream I — Motion Foundations

Status: Completed in Milestone 22 on 2026-08-01.

### Goal

Make common motion easy to author without adding application frame loops or a
general timeline framework.

### Contract Slice

- target-value animation that fits repeatable composition;
- stable retained identity, retargeting, interruption, and cancellation;
- scalar, color, opacity, translation, and scale interpolation;
- standard easing curves and theme-driven durations;
- paint-transform behavior for clipping, hit testing, and semantics;
- a move-safe handle for explicit cancellation and bounded repetition;
- reduced-motion behavior and platform preference reporting.

### Scheduler Slice

- monotonic animation time supplied by the UI platform boundary;
- one next-frame deadline across active animations in each window;
- invalidation only while progress changes;
- no busy loop after the final value is reached;
- safe removal when an element or window is destroyed;
- deterministic time advancement in the headless backend.

### Control And Gallery Slice

- automatic hover, press, focus, and disabled transitions;
- popup entrance and exit motion without changing focus ownership;
- a genuinely animated indeterminate progress indicator;
- a Gallery Motion page for fades, translation, color, easing, interruption,
  repetition, and reduced-motion behavior;
- concise examples that do not require developers to manage timers.

### Focused Tests

- exact interpolation at start, intermediate, and final timestamps;
- retargeting begins from the currently presented value;
- cancellation and unmounting leave no stale element access;
- completed animations stop requesting frames;
- multiple windows keep independent lifetimes and a common time contract;
- reduced motion presents the final state immediately;
- transformed visuals preserve the documented clipping and hit-test behavior.

### First Acceptance Gate

A public Gallery example can animate opacity, translation, and control-state
colors with no application-owned frame callback, then becomes immediate when
reduced motion is enabled.

## Workstream J — Extensible Motion Engine

### Goal

Turn the Milestone 22 motion foundation into one open, typed engine that can
animate application-defined curves, values, and custom-control properties.

### Curve And Timing Slice

- extensible `EasingCurve` with allocation-free built-ins and an immutable
  custom implementation boundary;
- cubic Bézier and stepped curves with deterministic evaluation;
- tween and spring timing as separate public models;
- explicit overshoot, finite-output, exception, identity, and ownership rules;
- no per-frame allocation for built-in curves or active timing evaluation.

### Property And Interpolation Slice

- public typed interpolator customization;
- stable typed animation-property keys;
- generic retained track storage replacing fixed runtime track members;
- built-in opacity, translation, scale, color, and scalar conveniences backed
  by the generic store;
- custom-control authoring and `CustomElementContext` access for custom animated
  properties;
- property-specific output constraints instead of globally clamping curve
  progress.

### Focused Tests

- custom curve and interpolator lifetime and identity;
- cubic Bézier, steps, spring settling, and deliberate overshoot;
- generic retargeting, repetition, cancellation, and unmounting;
- stable custom-property identity through recomposition;
- no idle deadlines and no built-in per-frame allocations;
- deterministic diagnostics for built-in and application-defined tracks.

### First Acceptance Gate

A Gallery custom control animates an application-defined property with an
application-defined curve, using no runtime source changes or manual frame
callback.

## Workstream K — Awaitable Motion And Orchestration

### Goal

Add safe imperative orchestration for workflows that naturally need to await,
sequence, combine, interrupt, or cancel motion while preserving retained UI
identity and the single motion scheduler.

### Controller And Async Slice

- retained `MotionController` binding to keyed element identity;
- `NGIN::Async::Task` and `TaskContext` integration;
- generic `AnimateToAsync` and fade, translation, scale, and color helpers;
- sequential `co_await` and parallel `WhenAll`/`WhenAny` composition;
- one-writer rules between declarative state and controller targets;
- completed, canceled, interrupted, and unmounted outcomes.

### Lifetime And Scheduler Slice

- task cancellation stops the corresponding track;
- retargeting completes the previous waiter as interrupted;
- unmount, window close, controller destruction, and application shutdown
  release continuations safely;
- reduced motion presents final values immediately and resumes through the UI
  scheduler rather than paint;
- async operations reuse the generic tracks, timing, platform clock, deadline,
  diagnostics, and idle behavior from Workstreams I and J.

### Focused Tests

- successful await and ordered sequence completion;
- parallel completion through `WhenAll` and first completion through `WhenAny`;
- cancellation, interruption, unmounting, and shutdown;
- immediate reduced-motion completion without a frame request;
- continuation thread/lane, reentrancy, and exception containment;
- no stale waiter or retained UI lifetime after completion.

### First Acceptance Gate

A public Gallery example awaits a fade and translation sequence, runs two
properties in parallel, cancels an operation, and becomes immediate under
reduced motion without owning a timer or frame loop.

## Sequence

### Wave 0 — Decisions And Baselines

1. Record current atlas, text, image, layout, collection, and gallery budgets.
2. Approve the glyph-storage design.
3. Prepare, but do not enact, font and codec dependency proposals.
4. Approve the accessibility provider boundary.

### Wave 1 — Reliability

1. Workstream A: glyph storage and text rendering.
2. Workstream H first slice: locked-stage diagnostics.
3. Close and commit Milestone 17.

### Wave 2 — Content

1. Approve exact font assets.
2. Implement Workstream B.
3. Approve and implement Workstream C.
4. Close and commit Milestone 18.

### Wave 3 — Desktop Foundations

Workstreams D and E may proceed in parallel after their contracts are approved.
Close and commit Milestones 19 and 20 independently.

### Wave 4 — Scale

1. Implement Workstream F on the stable layout and semantics contracts.
2. Add `TreeView` only if it does not destabilize virtualization.
3. Close and commit Milestone 21.

### Wave 5 — Motion

1. Approve the target-value API and transform behavior.
2. Implement Workstream I.
3. Add the public Gallery Motion page and deterministic coverage.
4. Close and commit Milestone 22.

### Wave 6 — Extensible Motion

1. Approve the curve, timing, interpolator, and typed-property contracts.
2. Implement Workstream J on the existing deterministic scheduler.
3. Add custom curve, spring, and custom-property Gallery examples.
4. Close and commit Milestone 23.

### Wave 7 — Awaitable Motion

1. Approve controller ownership, completion, cancellation, and UI-resumption
   rules.
2. Implement Workstream K using `NGIN::Async`.
3. Add sequential, parallel, interruption, and cancellation Gallery examples.
4. Close and commit Milestone 24.

### Wave 8 — Product Completion

1. Complete Workstream G.
2. Complete remaining packaging and release work in Workstream H.
3. Run the release gates.
4. Close and commit Milestone 25.

## Verification Matrix

| Change area | Minimum verification |
|---|---|
| Text layout or atlas | `NGINUITests`, text stress test, gallery Typography smoke |
| Renderer sampling/resource lifetime | `NGINUITests`, SDL3 contract test, native gallery capture |
| Fonts or codecs | focused decode/coverage tests, staged asset/license inspection, gallery smoke |
| Accessibility contract | headless semantic/provider tests |
| Windows UI Automation | provider automation test and manual Narrator checklist |
| Layout | focused layout tests and narrow-window gallery smoke |
| Virtualization | focused identity/scroll tests and 100,000-item benchmark |
| Extensible motion | curve/timing/interpolator/property tests, allocation checks, Gallery custom-motion smoke |
| Async motion | task completion/cancellation/lifetime tests, reduced-motion coverage, Gallery orchestration smoke |
| CLI staging diagnostic | `NGINCliTests` and hosted/standalone locked-artifact smoke |
| Public API | docs/API comments, install/export consumer, compatibility review |

Do not run every repository test after each edit. Batch each milestone and use
one targeted verification pass, escalating only when the touched ownership
boundary requires it.

## Milestone Closure Checklist

- implementation is inside the correct ownership boundary;
- public contracts have focused tests;
- gallery examples use public APIs;
- documentation describes actual behavior;
- dependencies and licenses have explicit approval and authored metadata;
- generated build output was not edited;
- targeted verification passes;
- known limitations are recorded;
- roadmap/progress evidence is updated;
- the completed milestone is committed before the next milestone begins.
