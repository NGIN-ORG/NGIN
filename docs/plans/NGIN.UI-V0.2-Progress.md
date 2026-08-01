# NGIN.UI Version 0.2 Progress

Roadmap: [`NGIN.UI-V0.2-Roadmap.md`](NGIN.UI-V0.2-Roadmap.md)

## Milestone 17 — Text And Renderer Reliability

Status: Complete
Completed: 2026-07-26

Delivered:

- replaced the single glyph texture with a configurable, fixed page budget;
- added least-recently-used page rebuilding protected by leases retained in
  runtime glyphs and display lists;
- released old display references before relayout so unused pages can be
  safely reused;
- kept native glyph textures nearest-filtered and physical-pixel aligned while
  images remain linearly filtered;
- added page, occupancy, size, eviction, rebuild, failure, and restoration
  diagnostics to the public text system and Gallery;
- added explicit text device-loss and restoration hooks plus application-wide
  invalidation, with automatic hosting integration;
- added bounded churn, live-page, fallback, multiple-window invalidation,
  device restoration, DPI, antialiasing, descender, wrapping, centering, and
  clipping coverage;
- changed generated CLI staging to keep the compiled artifact and report the
  exact source, destination, likely running-process lock, copy error, and retry
  action.

Default texture budget:

- page size: 1024 by 1024 R8;
- maximum pages: 4;
- maximum glyph texture storage: 4 MiB;
- pages are allocated only when needed.

Verification:

- `NGINUITests`: 115/115 passed, including allocation benchmarks;
- SDL3 backend contracts: 1/1 passed;
- hosted UI lifecycle: 1/1 passed;
- CLI staging generator test: 17 assertions passed;
- standalone Gallery build and `--smoke`: passed;
- hosted Gallery build and `--smoke`: passed;
- a live Windows Gallery executable produced the expected kept-source,
  destination, running-process, copy-error, and retry diagnostic;
- public API documentation coverage: 253 types documented.

## Milestone 18 — International Text And Practical Images

Status: Complete
Completed: 2026-07-27

Delivered:

- packaged Noto Sans Arabic and Noto Sans Symbols 2 behind the existing
  Noto Sans primary face, with application fallbacks taking precedence;
- staged all three OFL-licensed fonts, their license texts, and exact
  third-party revision and checksum records;
- added public font-coverage diagnostics for loaded family, style, source
  path, fallback use, resolved character count, and missing codepoints;
- defined the Gallery text promise as Latin, Greek, Cyrillic, Arabic,
  bidirectional text, combining sequences, and selected monochrome symbols;
- made color emoji explicitly unsupported in version 0.2 and observable as
  missing coverage instead of presenting a replacement glyph as success;
- added a backend-independent `StandardImageDecoder` for PNG, JPEG, and the
  existing PPM path while preserving asynchronous sources and cancellation;
- normalized decoded images to straight RGBA8, with opaque JPEG alpha;
- bounded encoded input to 64 MiB, decoded pixels to 256 MiB, and image
  dimensions to 16,384 pixels;
- pinned stb_image 2.30 at revision
  `013ac3beddff3dbffafd5177e7972067cd2b5083`, selected its MIT license, and
  kept it private to the NGIN.UI implementation;
- added a staged 1536 by 1024 PNG Gallery image and demonstrations of contain,
  cover, and tinted image rendering;
- updated the Typography, Images, and Diagnostics pages plus the richer-content
  and public API documentation.

Approved asset budget:

- Noto Sans Arabic variable font: 844,676 bytes,
  SHA-256 `63111B5B2E074DD48CC67692E0A2726D86EE94C1C37FE8598257B7B4E87E869E`;
- Noto Sans Symbols 2: 1,233,128 bytes,
  SHA-256 `7D5FB73B7CA67A6798101741F5D280A3D016A56A197AFCD4199DBB57B4B82A21`;
- total staged font files, including the existing Noto Sans primary:
  4,126,900 bytes;
- stb_image header: 283,010 source bytes,
  SHA-256 `594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3`;
- Gallery sample PNG: 2,264,488 bytes,
  SHA-256 `6BFB6C7FA132A79619A8B146550931B53B08259941921EDFABB92BBBEA166F0E`.

Verification:

- all three Gallery manifests validated;
- `NGINUITests`: 119/119 passed, including deterministic PNG/JPEG,
  malformed-input, cancellation, fallback-coverage, and missing-emoji tests;
- standalone Gallery build and `--smoke`: passed;
- hosted Gallery build and `--smoke`: passed;
- Gallery headless checks: passed;
- staged output contains all fonts, the sample PNG, OFL texts, stb license, and
  third-party notice;
- contracts-only installed-package consumer: 1/1 passed;
- public API documentation coverage: 247 types documented.

## Milestone 19 — Windows Accessibility Bridge

Status: Complete
Completed: 2026-07-30

Delivered:

- added a provider-neutral accessibility boundary built around immutable,
  revisioned semantic snapshots rather than runtime-tree access;
- added snapshot diffs for property, focus, structure, selection, and
  live-region changes;
- routed provider actions through a thread-safe application queue and the
  normal control action path on the UI thread;
- added native-window discovery to the platform contract and Win32 `HWND`
  discovery to the SDL3 backend;
- added the `NGIN.UI.Accessibility.Windows` package with a Windows UI
  Automation fragment provider;
- mapped semantic roles, names, descriptions, values, ranges, enabled/focus
  state, checked/selected/expanded state, collection position, live settings,
  DPI-aware bounds, and supported actions;
- exposed Invoke, Toggle, SelectionItem, RangeValue, Value, ExpandCollapse,
  ScrollItem, and VirtualizedItem patterns when the semantic node supports
  them;
- raised native focus, property, structure, selection, and live-region events
  from semantic snapshot changes;
- kept password values out of semantic snapshots and made detached windows and
  removed elements return `UIA_E_ELEMENTNOTAVAILABLE`;
- added capability, window, snapshot, event, action, and failure diagnostics;
- added an Accessibility Gallery page with native provider status, a button,
  checkbox, switch, slider, editable text, and a polite live region;
- enabled the provider in both standalone and hosted Gallery applications;
- documented provider setup, custom-control actions, unsupported-platform
  behavior, and a ten-step Windows Narrator checklist.

Verification:

- `NGINUITests`: 122/122 passed with 4,509 assertions, including snapshot
  diffs, UI-thread action dispatch, password privacy, and stale-window actions;
- native Windows UI Automation provider test passed: the client read the root,
  found a named button, invoked it through `InvokePattern`, and observed the
  detached element become unavailable;
- standalone Gallery manifest validated, built, and passed the Accessibility
  page smoke run;
- hosted Gallery manifest validated, built, and passed the Accessibility page
  smoke run;
- headless Gallery built and passed all twelve pages;
- public API documentation coverage: 272 types documented.

The manual Narrator checklist is ready for an interactive release pass. It was
not executed by the automated, non-interactive verification environment.

## Milestone 20 — Desktop Layout Primitives

Status: Complete
Completed: 2026-08-01

Delivered:

- added public `Grid` composition with fixed, automatic, and weighted tracks,
  track bounds, row/column placement, and spans;
- made Grid resolve intrinsic content consistently under finite and unbounded
  constraints, including use inside scrolling content;
- added public horizontal and vertical `WrapPanel` composition with item gaps,
  line gaps, and start, center, end, or space-between line alignment;
- added bounded `Canvas` composition with child offsets, optional desired-size
  contribution, and default clipping;
- retained resolved Grid tracks and WrapPanel lines in runtime layout state and
  exposed them through per-frame layout diagnostics;
- changed the Gallery shell to a two-column Grid and reduced its supported
  minimum window size to 640 by 480;
- expanded the Layout page with a settings form, responsive toolbar, dashboard
  tile layout, Canvas diagram, and nested scrolling example;
- added diagnostics and tests for spans, track bounds, finite/unbounded
  measurement, nested scrolling, flex-sized tracks, collapsed content,
  clipping, fractional DPI layouts, and keyed reconciliation.

Verification:

- `NGINUITests` layout primitive tests passed;
- Gallery headless checks passed at both 1180 by 760 and 680 by 520, including
  public primitive discovery, resolved diagnostics, and multi-line toolbar
  wrapping;
- standalone and hosted Gallery smoke checks passed;
- public API documentation coverage passed.

## Milestone 21 — Virtualized Collections

Status: Complete
Completed: 2026-08-01

Delivered:

- added a reusable fixed-row virtualization controller with viewport range
  calculation, configurable overscan, logical extent, and bounded realized
  mappings;
- added `IVirtualizedDataSource<T>` stable keys and labels plus range
  cancellation to the incremental-source contract;
- added `VirtualizedListView` as an opt-in adapter while leaving ordinary
  in-memory `ListView` composition unchanged;
- limited virtualized composition, measurement, painting, and semantic nodes
  to the viewport plus overscan instead of walking the logical source;
- retained source index, stable key, realized node, selected item, list focus,
  and semantic identity across viewport changes;
- anchored the top visible key and within-row offset through insertion,
  removal, reordering, filtering, and asynchronous source revisions;
- retained invalidations raised inside a frame so layout-discovered viewport
  changes compose their new realized range on the next immediate frame;
- added logical arrow, Home, End, type-ahead, selection, ensure-visible, and
  accessibility actions for items that are not currently realized;
- exposed logical count, realized range and mappings, extents, source revision,
  overscan, and range request/cancellation counts through controller and
  per-frame layout diagnostics;
- added a Collections Gallery card with a 100,000-item logical source, live
  realization/range-load counts, keyboard navigation, and an insert-above
  scroll-anchoring demonstration;
- published a Release time/allocation budget for the complete 100,000-item
  virtualized composition, reconciliation, and layout path;
- documented the fixed-size version 0.2 contract, source lifetime and revision
  rules, stable keys, asynchronous range loading, diagnostics, navigation, and
  accessibility behavior.

Verification:

- `NGINUITests`: 131/131 passed with 4,631 assertions, including 100,000-item
  realization bounds, retained keyed identity, source mutation anchoring,
  asynchronous range arrival, type-ahead, selection, ensure-visible, and
  virtual semantic identity;
- Release `virtual-list-100000` benchmark passed at 0.2950 ms and 31 ordinary
  heap allocations against budgets of 50 ms and 5,000 allocations; the gate
  also requires no more than 40 live runtime nodes;
- Gallery headless checks passed, including the 100,000-item bound, End-key
  navigation, stable focus/selection, insert-above anchoring, and range-load
  diagnostics;
- standalone and hosted Gallery Collections-page smoke runs passed;
- public API documentation coverage passed with 298 documented types.

## Milestone 22 — Motion Foundations

Status: Complete
Completed: 2026-08-01

Delivered:

- added declarative target-value motion for scalar values, opacity,
  translation, scale, backgrounds, foregrounds, and border colors with five
  standard easing choices;
- retained motion by stable element identity so recomposition retargets from
  the presented value and unmounting safely discards active state;
- added move-safe cancellation handles, bounded restart and reverse
  repetition, first-mount values, delays, and completion callbacks;
- scheduled one monotonic next-frame deadline per window only while motion is
  active, with no application-owned frame loop and no idle redraw requests;
- made translated and scaled visuals behave consistently across painting,
  child clipping, pointer hit testing, custom-control coordinates, and semantic
  bounds while leaving layout unchanged;
- added theme-duration transitions for button and text-input hover, press,
  focus, and disabled colors, plus popup entrance/exit and determinate and
  indeterminate progress motion;
- extended the platform contract with a monotonic clock and reduced-motion
  preference, including Windows system preference support in the SDL3 backend
  and deterministic time and preference controls in the test backend;
- added per-window motion control and diagnostics for active animations,
  motion frames, deadlines, and reduced-motion state;
- added a public Motion Gallery page showing retargetable fade, translation,
  scale, and color changes, easing comparisons, cancellable reverse
  repetition, an animated progress indicator, reduced motion, and popup
  entrance and exit;
- documented the target-value API, custom-control motion values, repetition,
  cancellation, transform behavior, reduced motion, deterministic testing, and
  backend requirements.

Verification:

- `NGINUITests`: 137/137 passed with 4,714 assertions, including exact
  interpolation, retargeting, cancellation, repetition, unmounting,
  multi-window deadlines, reduced motion, transformed clipping, hit testing,
  semantics, control-state motion, and popup exit lifetime;
- Gallery headless checks passed for public motion composition, interruption,
  cancellation, progress motion, and reduced-motion settling;
- standalone and hosted Gallery smoke builds and launches passed;
- SDL3 backend contract build and test passed;
- public API documentation coverage passed with 307 documented types.

## Milestone 23 — Extensible Motion Engine

Status: Complete
Completed: 2026-08-01

Delivered:

- replaced the closed easing enum with a copyable `EasingCurve` value whose
  built-ins remain allocation-free and whose immutable custom implementations
  have defined ownership, identity, input, exception, finite-output, and
  thread-safety rules;
- added linear, standard, ease-in, ease-out, ease-in-out, cubic Bézier, and
  stepped curves through one evaluator, including deliberate finite
  overshoot;
- separated fixed-duration `TweenTiming` from physical `SpringTiming`, with
  bounded analytical spring sampling and deterministic rest thresholds;
- added the public `AnimationInterpolator<T>` and `AnimationValuePolicy<T>`
  customization points for application-owned values and output constraints;
- replaced fixed runtime motion fields with type-erased, typed property tracks
  keyed by stable `AnimationPropertyId` values while preserving convenient
  built-in opacity, transform, color, and scalar declarations;
- allowed custom controls to declare properties through `MotionProperties::Set`
  and read their presented value and active state from
  `CustomElementContext`;
- kept built-in and custom tracks on the same retargeting, cancellation,
  repetition, reduced-motion, lifetime, scheduler, and idle-deadline paths;
- added per-track diagnostics for owner, property identity and name, value and
  interpolator type, timing, curve, custom-curve use, activity, evaluation
  failures, and property conflicts;
- expanded the Gallery Motion page with an obvious custom overshoot, a
  four-slider cubic Bézier editor, spring motion, and a custom-painted dial
  driven by its own animation property;
- rewrote the motion guide for curves, springs, typed values, custom
  properties, diagnostics, error rules, and the intentional migration from the
  closed Milestone 22 API without retaining a legacy engine.

Verification:

- `NGINUITests`: 140/140 passed with 4,762 assertions, including custom curve
  containment, cubic Bézier inversion, steps, spring sampling, overshoot
  policy, typed interpolation, property identity, diagnostics, retargeting,
  cancellation, unmounting, reduced motion, and idle completion;
- standalone Gallery product-first build passed and its staged executable
  completed `--smoke` successfully;
- public API documentation coverage passed with 322 documented types.

## Milestone 24 — Awaitable Motion And Orchestration

Status: Complete
Completed: 2026-08-01

Delivered:

- added a retained, move-only `MotionController` that attaches during
  composition and binds to stable element identity without exposing runtime
  node pointers;
- added generic `AnimateToAsync` plus fade, translation, scale, and color
  conveniences returning normal `NGIN::Async::Task<MotionOutcome>` values;
- added an application-owned UI executor and `Application::CreateTaskContext`
  so continuations resume from the event loop rather than paint or a
  cancellation thread;
- defined `Completed`, `Canceled`, `Interrupted`, and `Unmounted` outcomes,
  including deterministic interruption when a newer target replaces an older
  operation;
- linked task cancellation to track cancellation and released waiters during
  element removal, window closure, controller destruction, and application
  shutdown;
- made declarative targets the explicit owner when declarative and controller
  code name the same property, preventing two writers from silently fighting;
- kept controller motion on the same generic tracks, timing types, platform
  clock, deadlines, diagnostics, reduced-motion policy, and render path as
  declarative motion;
- corrected `NGIN::Async::WhenAll` to start all cold child tasks before waiting
  for their results, enabling real parallel motion while retaining normal
  completion and error propagation;
- added deterministic coverage for built-in and custom properties, ordered
  sequences, parallel composition, cancellation, interruption, unmounting,
  reduced motion, window closure, and application shutdown;
- expanded the Gallery Motion page with visible awaited steps, parallel
  movement, slow cancellation, replacement, result text, and instructions for
  trying reduced motion;
- documented declarative and awaited authoring, controller lifetime, property
  ownership, outcomes, cancellation, `WhenAll`, `WhenAny`, and reduced motion.

Verification:

- `NGINUITests`: 147/147 passed with 4,866 assertions;
- standalone Gallery product-first build passed and its staged executable
  completed `--smoke` successfully;
- the Gallery headless product built from its manifest and all headless checks
  passed;
- public API documentation coverage passed with 329 documented types;
- the awaited-motion test suite exercises sequential and parallel tasks on the
  deterministic UI clock, including the corrected `WhenAll` start behavior.

## Milestone 25 — Version 0.2 Product Completion

Status: Complete
Completed: 2026-08-01

Delivered:

- versioned `NGIN.UI`, SDL3 backend, hosting, and Windows accessibility package
  and CMake identities as 0.2.0, with matching header constants, workspace
  policy, Gallery product versions, and 0.2 dependency ranges;
- added searchable Gallery navigation by page name or feature, an explicit
  no-results message, and one concise public API example with a working copy
  action on every page;
- kept the search, examples, feature pages, and model in the shared Gallery
  source used by standalone, hosted, and headless products;
- bounded `ImageTextureCache` to 128 textures and 256 MiB by default, with
  expired-resource cleanup, least-recently-used eviction, configurable limits,
  and current, peak, eviction, and capacity diagnostics;
- added image-cache occupancy and pressure to the Gallery Diagnostics page;
- published one 0.2 release guide containing package ranges, accepted motion
  migration, exact performance/allocation/resource budgets, verification
  commands, and archive instructions;
- added Release profiles to all Gallery products and a versioned `demo`
  publisher whose archive contains the executable, runtime libraries, image,
  fonts, NGIN license, OFL licenses, SDL license, dependency notices, and a
  source-independent run guide;
- expanded the UI CI matrix to build and smoke standalone, hosted, and
  headless Release Gallery products on Windows, Linux, and macOS, run the
  Windows UI Automation provider check, and upload the versioned Windows demo;
- updated the installed consumer to require NGIN.UI 0.2 and included Windows
  accessibility headers in the public documentation gate.

Published budgets:

- composition-2000: 250 ms and 20,000 allocations;
- layout-2000: 250 ms and 2,000 allocations;
- text-paragraph: 500 ms and 20,000 allocations;
- large-list-10000: 2,000 ms and 450,000 allocations;
- virtual-list-100000: 50 ms, 5,000 allocations, and at most 40 live nodes;
- glyph atlas: four 1024 by 1024 R8 pages, or 4 MiB;
- image cache: 128 resident textures and 256 MiB RGBA8;
- staged font files: 4,126,900 bytes.

Verification:

- `NGINUITests`: 148/148 passed with 4,891 assertions, including image-cache
  entry, memory, LRU, expired-resource, and over-budget behavior;
- Release budgets passed locally at 5.3703 ms/22 allocations for composition,
  0.0572 ms/0 for layout, 1.3355 ms/7,830 for text, 72.534 ms/54 for the
  ordinary list, and 0.2995 ms/31 for the 100,000-item virtualized list;
- all three Release Gallery manifests validated and built from the authored V4
  project files;
- standalone and hosted native Release `--smoke` runs passed on Windows, and
  the Release headless Gallery checks passed;
- the Windows UI Automation provider test passed 1/1;
- the public API documentation gate passed with 330 documented types;
- NGIN.Base and NGIN.UI installed into a fresh prefix, and the independent
  `find_package(NGINUI 0.2)` consumer passed 1/1;
- `dist/NGIN.UI.Gallery-0.2.0-demo.zip` was produced with 20 staged entries,
  its required assets and notices were checked, and a fresh isolated extraction
  completed `--smoke` successfully;
- the Linux and macOS native checks are represented in the committed CI matrix;
  this Windows development session did not execute those operating systems.
