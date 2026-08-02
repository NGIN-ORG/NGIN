# NGIN.UI API Reference {#mainpage}

NGIN.UI is a retained, backend-neutral C++23 desktop UI toolkit.

The current public header and package version is 0.4.0. See the
[version 0.4 release notes](../guides/ngin-ui-v0.4-release.md) for package
ranges, migration, resource budgets, and release verification.

This reference is generated from the installed public headers in
`Packages/NGIN.UI/include/NGIN/UI`. Start with the
[developer guide index](../guides/ngin-ui.md) for task-oriented documentation
and use this reference for exact signatures, ownership, and result types.

## Primary entry points

- `NGIN::UI::CreateApplication()` and `NGIN::UI::Application`
- `NGIN::UI::Window`
- `NGIN::UI::Composer`
- `NGIN::UI::GridTrack`, `NGIN::UI::WrapPanelProperties`, and
  `NGIN::UI::CanvasPlacement`
- `NGIN::UI::FixedVirtualizedListController`,
  `NGIN::UI::IVirtualizedDataSource<T>`, and
  `NGIN::UI::VirtualizedListView()`
- `NGIN::UI::NodeProperties`
- `NGIN::UI::Animate()`, `NGIN::UI::MotionController`, and
  `NGIN::UI::Application::CreateTaskContext()`
- `NGIN::UI::State<T>`, `NGIN::UI::ReadOnlyBinding<T>`, and
  `NGIN::UI::ComputedState<T>`
- `NGIN::UI::Command`, `NGIN::UI::AsyncCommand`, and
  `NGIN::UI::CommandBinding`
- `NGIN::UI::ValidationField<T>` and `NGIN::UI::ValidationForm`
- `NGIN::UI::ViewModelTaskScope`, `NGIN::UI::KeyedViewModelHost<T>`, and
  `NGIN::UI::AsyncPresentation<T>`
- `NGIN::UI::PageRegistry`, `NGIN::UI::NavigationService`, and
  `NGIN::UI::NavigationHost`
- `NGIN::UI::NativeTextSystem`
- `NGIN::UI::ImageResource`, `NGIN::UI::StandardImageDecoder`, and
  `NGIN::UI::ImageTextureCache`
- `NGIN::UI::Testing::SoftwareRenderBackend` and
  `NGIN::UI::Testing::CompareVisuals()`
- `NGIN::UI::Hosting::ConfigureUIHosting()` and
  `NGIN::UI::Hosting::IUIDispatcher`
- `NGIN::UI::SDL3::CreatePlatformBackend()` and
  `NGIN::UI::SDL3::CreateRendererBackend()`
- `NGIN::UI::Accessibility::Windows::CreateAccessibilityBackend()`

## Control families

- `Controls.hpp`: checkbox, radio, switch, slider, progress, labels, tooltips
- `Collections.hpp`: selection models, ordinary and virtualized lists,
  incremental sources, combo boxes, tabs, menus
- `Virtualization.hpp`: fixed-row viewport ranges, overscan, stable-key
  anchoring, logical navigation, and realization diagnostics
- `Composer.hpp`: primitive, Grid, WrapPanel, Canvas, text, text-area, image,
  popup, and custom elements
- `LayoutPrimitives.hpp`: grid tracks and spans, wrapping rules, and absolute
  canvas placement
- `CustomElement.hpp`: custom measurement, painting, input, semantics, state
- `Animation.hpp` and `Motion.hpp`: target values, easing, awaitable motion,
  cancellation, repetition, transforms, and reduced-motion behavior

## Integration contracts

- `IPlatformBackend` normalizes windows and input.
- `IRenderBackend` consumes render packets and owns device handles.
- `IFontProvider`, `ITextShaper`, `ITextLayout`, `ITextGeometry`, and
  `IGlyphAtlas` separate text policy from retained elements.
- `IImageDecoder` and `IImageResolver` separate encoded/logical images from
  renderer uploads.
- `IAccessibilityBackend` consumes immutable semantic snapshots and posts
  actions without accessing retained runtime nodes.

`NativeTextSystem::CoverageDiagnostics()` reports loaded font faces, fallback
use, and missing characters. The packaged font order covers the Gallery's
Latin, Greek, Cyrillic, Arabic, combining-mark, bidi, and symbol examples.
Color emoji is explicitly unsupported in version 0.3.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) is the canonical public-API
usage reference.

The [testing and release guide](../guides/ngin-ui-testing-and-release.md)
documents deterministic visual tests, performance budgets, cache diagnostics,
device recreation, and installed CMake consumption.
