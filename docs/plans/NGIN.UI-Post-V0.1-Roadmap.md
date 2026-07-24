# NGIN.UI Post-v0.1 Roadmap

Status: Active — Milestone 15 complete
Baseline: [`NGIN.UI-V0.1-Acceptance-Audit.md`](NGIN.UI-V0.1-Acceptance-Audit.md)

## Goal

Turn the version 0.1 architectural implementation into a convincing desktop UI
toolkit: a visually useful control gallery, a supported custom-control surface,
a broader set of semantic controls, and documentation suitable for application
developers.

The gallery should be a catalogue rather than a single crowded screen. Every
public control should have a discoverable example showing its important states,
keyboard behavior, semantics, and the public code needed to compose it.

## Current Baseline

| Area | Implemented | Demonstrated in gallery |
|---|---:|---:|
| Text and Unicode shaping | Yes | Yes |
| Button | Yes | Yes |
| Single-line `TextField` | Yes | Yes |
| Row, column, overlay, padding, and alignment | Yes | Yes |
| Scroll view | Yes | Yes |
| Popup | Yes | Yes |
| Dialog and multiple windows | Yes | Yes |
| Themes and resource scopes | Yes | Yes |
| Semantics and inspector overlays | Yes | Yes |
| Border and rounded control chrome | Yes | Yes |
| Checkbox, radio, toggle, slider, and progress controls | Yes | Yes |
| Lists, selection, combo box, tabs, and menus | Yes | Yes |
| Multiline text and `TextArea` | Yes | Yes |
| Logical images and `Image` | Yes | Yes |
| Public custom measurement and painting | Yes | Yes |
| Application-developer guide | Yes | Yes |

## Milestone 9 — Visual Styling Primitives

- [x] Add border color, per-edge thickness, and corner-radius properties.
- [x] Lower borders and rounded backgrounds through the existing display list.
- [x] Define visual states for normal, hovered, pressed, focused, disabled,
      selected, invalid, and read-only controls.
- [x] Add theme tokens for surfaces, text, accent, borders, focus, selection,
      spacing, radii, and standard control sizes.
- [x] Add a visible, keyboard-safe focus indicator.
- [x] Add a `Border`/panel composition helper and a `Separator` primitive.
- [x] Test DPI scaling, nested clipping, state transitions, and theme overrides.
- [x] Document the style-resolution and control-state rules.

Exit criterion: buttons, fields, panels, and focus states no longer rely on
hardcoded gallery colors, and all styling remains backend-neutral.

## Milestone 10 — Gallery Catalogue

- [x] Replace the single column with a responsive catalogue shell containing
      navigation and a scrollable content region.
- [x] Add pages for Overview, Layout, Typography, Inputs, Collections,
      Overlays, Windows, Resources, and Diagnostics.
- [x] Show every currently implemented feature before adding new controls:
      row/column/overlay alignment, scrolling, popup dismissal, modal dialogs,
      multiple windows, theme switching, resource scopes, semantics, and
      inspector overlays.
- [x] Add light and dark theme switching.
- [x] Add normal, hover, focus, pressed, disabled, validation-error, and
      read-only examples where applicable.
- [x] Keep `ComposeMainView()` shared by the standalone and hosted products.
- [x] Add a deterministic gallery model so examples can be exercised in
      headless tests.
- [x] Preserve `--smoke`, but make it visit or instantiate every gallery page.
- [x] Add screenshots to the README only after the catalogue layout stabilizes.

Exit criterion: a developer can discover every implemented version 0.1 feature
from the running gallery without reading private implementation code.

## Milestone 11 — Supported Custom-Control API

- [x] Define a public custom-element contract with constraint-based measurement,
      arrangement, painting, input, and semantics.
- [x] Add a bounded `PaintContext` that emits display-list commands in local
      coordinates while respecting inherited clips, transforms, and opacity.
- [x] Specify callback ownership, exceptions/errors, invalidation, and UI-thread
      requirements.
- [x] Provide generic retained local state keyed to element identity, without
      exposing runtime-node storage.
- [x] Allow composite controls to remain simple C++ composition functions.
- [x] Ensure a new custom control never requires changes to a platform or
      renderer backend.
- [x] Add a custom badge, progress ring, and small chart as public examples.
- [x] Test custom measurement, clipping, input routing, semantics, identity,
      unmount cleanup, and renderer-independent output.
- [x] Write the custom-control authoring guide before declaring this API stable.

Exit criterion: an application can implement a semantic, interactive,
custom-painted control using only public `NGIN.UI` APIs.

## Milestone 12 — Foundational Controls

- [x] `CheckBox`, including indeterminate state.
- [x] `RadioButton` and typed radio-group selection.
- [x] `ToggleSwitch`.
- [x] `Slider` with pointer capture, keyboard increments, and range semantics.
- [x] `ProgressBar`, including indeterminate presentation.
- [x] `Label` association for input controls.
- [x] `ToolTip` with delayed, non-focus-stealing popup behavior.
- [x] Visual scrollbars for `ScrollView`, with drag and keyboard behavior.
- [x] Consistent disabled, focus, validation, and theme behavior for every
      control.
- [x] Add each control and its state matrix to the gallery and headless tests in
      the same change.

Exit criterion: the toolkit covers the standard input controls needed by a
small settings-style desktop application.

## Milestone 13 — Collections and Navigation

- [x] Define typed selection models for zero, one, and multiple selection.
- [x] Add `ListView` and semantic `ListItem`.
- [x] Preserve keyed item identity across insert, remove, sort, and filtering.
- [x] Add keyboard navigation, type-ahead selection, and ensure-visible
      scrolling.
- [x] Add `ComboBox` using the popup and selection foundations.
- [x] Add `Tabs` with keyboard traversal and retained page state.
- [x] Add menu-button and context-menu foundations.
- [x] Define an incremental data-source boundary before implementing
      virtualization.
- [x] Add large-list performance tests before enabling virtualization.
- [x] Add interactive collection and navigation pages to the gallery.

Exit criterion: ordinary selectable collections and navigation work with mouse,
keyboard, semantics, and stable reconciliation.

## Milestone 14 — Richer Content

- [x] Add multiline text layout, Unicode line breaking, wrapping, and alignment.
- [x] Add font fallback while preserving HarfBuzz run shaping.
- [x] Add multiline `TextArea` editing, vertical caret navigation, and scrolling.
- [x] Introduce logical image resources independent of backend texture handles.
- [x] Support memory, file, and generated-pixel image sources.
- [x] Define asynchronous decode, cancellation, upload, and device-recreation
      behavior.
- [x] Add `Image` with fit, alignment, tint, clipping, and semantic description.
- [x] Add typography, text-area, and image pages to the gallery.

Exit criterion: the gallery can present realistic text-heavy and image-backed
application content without private resource or renderer access.

## Milestone 15 — Developer Documentation

- [x] Write a five-minute standalone “first window” guide.
- [x] Write the equivalent `NGIN.Core` hosted guide.
- [x] Document package and `.nginproj` setup.
- [x] Document composition, keys, reconciliation, and retained state.
- [x] Document layout constraints, alignment, DPI units, and scrolling.
- [x] Document state, bindings, validation, invalidation, and model ownership.
- [x] Document routed input, focus, commands, clipboard, and IME.
- [x] Document themes, resource scopes, semantics, and inspector tooling.
- [x] Document custom composite and custom-painted controls.
- [x] Document UI-thread and dispatcher rules.
- [x] Write platform- and renderer-backend authoring guides.
- [x] Add API reference comments to every public type and generate browsable API
      documentation.
- [x] Add troubleshooting pages for dependency restore, shaders, fonts, SDL,
      DPI, and backend startup errors.
- [x] Cross-link every guide to a buildable example.

Exit criterion: a developer unfamiliar with the implementation can build,
style, test, and debug an application without reading `NGIN.UI` source files.

## Milestone 16 — Release Quality

- [ ] Add a deterministic software/reference renderer for pixel tests.
- [ ] Add tolerant visual regression tests as a supplement to structural tests.
- [ ] Add gallery smoke coverage for resize, keyboard, pointer, clipboard, IME,
      popup, dialog, and multiple-window behavior.
- [ ] Add composition, layout, text, and large-list benchmarks with budgets.
- [ ] Track frame allocation counts and glyph/image cache behavior.
- [ ] Test renderer-device recreation and logical resource restoration.
- [ ] Exercise supported Windows, Linux, and macOS build paths in CI.
- [ ] Verify install/export consumption outside the source tree.
- [ ] Produce license notices and staged runtime assets from authored package
      metadata.
- [ ] Define source-compatibility and deprecation policy for the public API.

Exit criterion: the toolkit has repeatable cross-platform packaging,
performance baselines, and behavioral and visual regression coverage.

## Gallery Completion Rule

A public control is not gallery-complete until:

- [ ] its normal and meaningful alternate states are visible;
- [ ] mouse and keyboard operation are demonstrated;
- [ ] focus and disabled behavior are visible;
- [ ] semantic role, value, state, and actions are correct;
- [ ] theme tokens, rather than gallery-only colors, drive its appearance;
- [ ] the example uses only public APIs;
- [ ] focused headless behavior tests exist;
- [ ] standalone and hosted builds use the same example implementation.

## Deliberately Later

Native accessibility bridges, mobile backends, rich-text editing, docking,
data-grid virtualization, designer/hot reload, markup, advanced animation
authoring, remote rendering, and a stable plugin widget ABI remain outside this
roadmap unless promoted by a separate approved plan.

Any new third-party dependency or public manifest/schema change requires
explicit approval before implementation.
