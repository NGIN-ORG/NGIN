# NGIN.UI API Reference {#mainpage}

NGIN.UI is a retained, backend-neutral C++23 desktop UI toolkit.

This reference is generated from the installed public headers in
`Packages/NGIN.UI/include/NGIN/UI`. Start with the
[developer guide index](../guides/ngin-ui.md) for task-oriented documentation
and use this reference for exact signatures, ownership, and result types.

## Primary entry points

- `NGIN::UI::CreateApplication()` and `NGIN::UI::Application`
- `NGIN::UI::Window`
- `NGIN::UI::Composer`
- `NGIN::UI::NodeProperties`
- `NGIN::UI::State<T>` and `NGIN::UI::Binding<T>`
- `NGIN::UI::NativeTextSystem`
- `NGIN::UI::ImageResource` and `NGIN::UI::ImageTextureCache`
- `NGIN::UI::Hosting::ConfigureUIHosting()` and
  `NGIN::UI::Hosting::IUIDispatcher`
- `NGIN::UI::SDL3::CreatePlatformBackend()` and
  `NGIN::UI::SDL3::CreateRendererBackend()`

## Control families

- `Controls.hpp`: checkbox, radio, switch, slider, progress, labels, tooltips
- `Collections.hpp`: selection models, lists, combo boxes, tabs, menus
- `Composer.hpp`: primitive, text, text-area, image, popup, and custom elements
- `CustomElement.hpp`: custom measurement, painting, input, semantics, state

## Integration contracts

- `IPlatformBackend` normalizes windows and input.
- `IRenderBackend` consumes render packets and owns device handles.
- `IFontProvider`, `ITextShaper`, `ITextLayout`, `ITextGeometry`, and
  `IGlyphAtlas` separate text policy from retained elements.
- `IImageDecoder` and `IImageResolver` separate encoded/logical images from
  renderer uploads.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) is the canonical public-API
usage reference.
