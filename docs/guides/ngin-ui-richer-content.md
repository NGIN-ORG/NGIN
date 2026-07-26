# NGIN.UI Richer Content Guide

This guide covers multiline text, `TextArea`, font fallback, logical images,
asynchronous decoding, and renderer-device recreation.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) provides Typography,
Text Area, and Images pages for every API described here.

## Multiline text

`NativeTextSystem::LayoutParagraph()` treats CR, LF, and CRLF as mandatory line
breaks. With `TextWrapping::Wrap`, it also wraps at Unicode whitespace,
hyphens, slashes, and CJK/Hangul opportunities. Emergency wrapping stays on
grapheme boundaries, so a combining sequence or emoji ZWJ sequence is never
split merely to satisfy a width constraint.

Every visual line records its global UTF-8 byte range and every positioned
HarfBuzz run records its global byte offset. `CaretRect()` and `RangeRects()`
therefore remain valid across explicit line breaks, wrapping, bidi runs, and
font-fallback boundaries.

Use `TextElementProperties` to control wrapping, line height, and per-line
alignment:

```cpp
NGIN::UI::NodeProperties textProperties{};
textProperties.layout.preferredSize.width = 520.0F;
textProperties.text.wrapping = NGIN::UI::TextWrapping::Wrap;
textProperties.text.alignment = NGIN::UI::TextAlignment::Center;
textProperties.text.lineHeight = 24.0F;
textProperties.text.geometry = &text;

composer.Text(
    NGIN::Text::String{"A wrapped paragraph\nwith an authored line break."},
    text, text, textProperties, "paragraph");
```

`TextWrapping::NoWrap` still honors authored line breaks. It disables automatic
wrapping and is useful with a horizontally scrollable `TextArea`.

## Font fallback

The first font passed to `NativeTextSystem::Create()` is the primary face.
Additional paths form an ordered fallback chain:

```cpp
auto created = NGIN::UI::NativeTextSystem::Create(
    application.Renderer(),
    NGIN::UI::NativeTextCreateInfo{
        .fontPath = NGIN::Text::String{"fonts/Inter.ttf"},
        .fallbackFontPaths = {
            NGIN::Text::String{"fonts/NotoSansArabic.ttf"},
            NGIN::Text::String{"fonts/NotoSansCJK.ttf"},
        },
    });
```

Fallback is selected per grapheme cluster. Adjacent clusters using the same
face are regrouped into a HarfBuzz span, preserving word shaping instead of
rasterizing code points independently. If no configured face covers the whole
cluster, the primary face supplies its missing-glyph presentation.

The empty primary path selects NGIN.UI's bundled OFL-licensed Noto Sans
variable font. Applications own and license any additional fallback files they
configure.

## Glyph storage

Native text uses fixed-size R8 atlas pages. Pages are created only when needed
and never exceed `maximumAtlasPages`. The default is four 1024 by 1024 pages,
so glyph pixels use at most 4 MiB of renderer texture storage.

```cpp
auto created = NGIN::UI::NativeTextSystem::Create(
    application.Renderer(),
    NGIN::UI::NativeTextCreateInfo{
        .atlasSize = {1024, 1024},
        .maximumAtlasPages = 4,
    });
```

Old pages are rebuilt from least-recently-used storage only after no runtime
tree or display list still refers to their glyphs. If every page is still
visible, `ResolveGlyph()` reports capacity pressure instead of reusing a live
texture handle. `AtlasDiagnostics()` reports the page limit, active and peak
page counts, occupancy, stored pixel sizes, evictions, rebuilds, allocation
failures, and device restorations.

FreeType rasterizes at the active window scale. Glyph textures use nearest
sampling and their rendered quads are aligned to physical pixels. Image
textures keep their independent linear-sampling policy.

## TextArea

`Composer::TextArea()` uses the same typed `Binding<String>`, transactional
editing buffer, grapheme segmenter, selection, clipboard, and IME path as
`TextField`. It adds:

- Enter insertion;
- line-relative Home and End;
- Up and Down navigation that retains the preferred caret x position;
- multiline selection and composition geometry;
- vertical scrolling and optional horizontal scrolling;
- automatic caret reveal after layout.

```cpp
NGIN::UI::State<NGIN::Text::String> notes{
    NGIN::Text::String{"First line\nSecond line"}};

NGIN::UI::NodeProperties area{};
area.layout.preferredSize = {520.0F, 220.0F};
area.layout.padding = NGIN::UI::Thickness::Uniform(NGIN::UI::Dp{12.0F});
area.text.layout = &text;
area.text.geometry = &text;
area.text.glyphAtlas = &text;
area.text.wrapping = NGIN::UI::TextWrapping::Wrap;
area.text.lineHeight = 22.0F;
area.semantics.label = NGIN::Text::String{"Notes"};

composer.TextArea(NGIN::UI::Bind(notes), text, area, "notes");
```

The editing control is a semantic text box. Its semantic value follows the
binding unless password privacy is requested.

## Logical image resources

`ImageResource` retains decoded RGBA pixels and never exposes a renderer
texture handle. A resource can be created from:

- already-decoded pixels with `FromPixels()`;
- encoded memory with `DecodeMemoryAsync()`;
- a file with `DecodeFileAsync()`;
- a pixel callback with `GenerateAsync()`.

The built-in `PortablePixmapImageDecoder` accepts P6 and P3 PPM data. For PNG,
JPEG, WebP, or application-specific containers, implement `IImageDecoder` and
pass a shared decoder to the memory or file factory. This keeps format policy
out of the backend-neutral UI core.

Decode and generation work has four observable states: `Loading`, `Ready`,
`Failed`, and `Cancelled`. `Cancel()` is cooperative, `Wait()` joins outstanding
work, and `CopyPixels()` succeeds only in `Ready`. Completion does not call UI
objects from a worker thread; an application that does not wait should poll the
state and invalidate its window on the UI thread.

## Upload and device recreation

`ImageTextureCache` is the boundary between logical pixels and a render
backend. `Resolve()` uploads a ready resource on first use and reuses the
texture for later frames.

The cache and renderer must live on the UI/render thread. Handle device
transitions explicitly:

```cpp
imageCache.OnDeviceLost();                 // drops stale texture handles
text.OnDeviceLost();                       // drops stale glyph pages
// recreate or replace the render device
imageCache.OnDeviceRestored(renderer);     // future Resolve() reuploads pixels
auto restored = text.OnDeviceRestored(renderer);
```

Register `NativeTextSystem::SetResourcesInvalidatedCallback()` to invalidate
every window after either text-device hook. `NGIN.UI.Hosting` does this
automatically. A standalone application can connect it directly:

```cpp
text.SetResourcesInvalidatedCallback([&application] {
    application.InvalidateAll(NGIN::UI::InvalidationKind::All);
});
```

Destroy the caches and text system before their renderer. `OnDeviceLost()` is
also safe during shutdown and makes resource resolution report that no device
is available.

## Image element

`Composer::Image()` combines a logical resource and resolver with:

- `None`, `Fill`, `Contain`, `Cover`, and `ScaleDown` fit modes;
- normalized horizontal and vertical alignment;
- RGBA tint;
- optional clipping;
- a required meaningful semantic description for informative images.

```cpp
NGIN::UI::NodeProperties image{};
image.layout.preferredSize = {320.0F, 180.0F};
image.image.fit = NGIN::UI::ImageFit::Cover;
image.image.alignment = {0.5F, 0.25F};
image.image.tint = {1.0F, 0.9F, 0.85F, 1.0F};

composer.Image(resource, imageCache,
               NGIN::Text::String{"Sunset over a wooded ridge"},
               image, "hero-image");
```

Decorative images may set `properties.semantics.hidden = true`; otherwise the
description is exposed with the semantic image role.

The gallery's Typography, Text Area, and Images pages are executable examples
of these APIs.
