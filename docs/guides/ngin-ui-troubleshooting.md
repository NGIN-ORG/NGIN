# NGIN.UI Troubleshooting

Start with the complete structured error. Log `code`, `backend`, `operation`,
`logicalResource`, `nativeCode`, and `message`; the message alone often hides
which ownership boundary failed.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) and
[`NGIN.UI.Gallery.Tests`](../../Examples/NGIN.UI.Gallery.Tests/) separate native
backend failures from core composition failures.

## Package restore or project resolution

Symptoms:

- unknown `NGIN.UI`, `NGIN.UI.Backend.SDL3`, or `NGIN.UI.Hosting` package;
- no compatible version/provider;
- generated build omits the expected target.

Checks:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe validate `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --profile Debug

build/dev/Tools/NGIN.CLI/ngin.exe graph `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --profile Debug
```

Confirm that the workspace `PackageProviders` includes this repository's
`Packages/` wrappers and that the product uses product-first V4 `<Uses>` under
`<Application>`. Do not add a handwritten project CMake fallback.

Delete or repair only the exact generated output after confirming it is safe;
authored manifests are the source of truth.

## FreeType or HarfBuzz restore

The default `NGIN.UI` package fetches pinned sources. If network restore is
disabled, configure with `NGIN_UI_FETCH_THIRD_PARTY=OFF` and provide compatible
installed FreeType and HarfBuzz CMake packages.

An archive failure reporting “no space on device” is literal: HarfBuzz and SDL
clean stages can consume substantial temporary space. Reuse an existing build
tree or remove only known generated output.

## Font startup or missing glyphs

`NativeTextSystem::Create()` uses the bundled Noto Sans path when `fontPath` is
empty. Confirm the staged font exists and that the process working directory
does not invalidate an explicit relative path.

Missing-script glyphs require configured `fallbackFontPaths`. Fallback is not
an OS-global font search. Every shipped fallback must be packaged and licensed
by the application.

If text measures but does not paint, check:

- `text.layout` and `text.glyphAtlas` are non-null;
- the render backend can create/update R8 textures;
- the glyph atlas is not full;
- font size and scale factor are positive;
- text and clip bounds overlap.

## SDL platform startup

Typical causes:

- no graphical session;
- an unsupported video driver;
- the SDL runtime or native dependencies were not staged;
- window creation is attempted off the UI thread.

Use `NGIN.UI.Gallery.Tests` first. If it passes but the native gallery fails,
the view/core is healthy and the issue is in the SDL/native environment.

Record `SDL_GetError()` as `nativeCode`/`message` inside the backend boundary;
do not call SDL directly from application views.

## SDL_GPU renderer startup

Check that:

- SDL reports a GPU driver;
- the selected adapter supports the required texture/sample formats;
- renderer validation output is enabled;
- the window was created before its surface;
- the surface pixel extent is non-zero.

Remote desktop, virtual machines, and CI sessions may expose SDL windows but no
usable GPU device. Use the recording backend for headless CI and run native
smoke tests on a GPU-capable runner.

## Shader loading or pipeline creation

The current `NGIN.UI.Backend.SDL3` implementation uses
`SDL_CreateGPURenderer`; SDL owns its internal pipelines and NGIN.UI does not
stage shader files. If startup reports an SDL renderer or pipeline failure,
record the selected SDL GPU driver and adapter.

A custom backend built directly on raw `SDL_GPU` or another graphics API owns
its shader assets. Confirm that provider's staged paths and compiled formats
match the active graphics driver.

A shader/pipeline error should name:

- shader stage;
- source or staged logical resource;
- selected driver/format;
- native compilation or pipeline code.

Do not copy provider shaders into an application project to hide a staging
problem; repair the provider package contribution.

## DPI and blurry or misaligned content

Layout values are already device-independent. Do not multiply padding, sizes,
or pointer coordinates by the scale factor in application code.

Backends must:

- report the current window scale;
- convert native pointer pixels to logical units;
- emit scale-change events;
- size render targets in pixels;
- honor packet scale and scissor pixels.

Custom controls paint in logical local coordinates. Use scale only for
pixel-sensitive cached resources.

## Empty, clipped, or overlapping layout

Inspect `Window::Inspect()` and enable layout bounds. Common causes:

- both parent and scroll child are unbounded on the scroll axis;
- preferred size is zero and the element has no measurable content;
- maximum size is smaller than minimum/preferred size;
- a `Row` child consumes the unbounded main axis;
- text uses center/end alignment but its arranged container is unexpectedly
  narrow;
- `Collapsed` was used when `Hidden` layout retention was intended.

Check `LastLayoutStats()` and node `measuredSize`/`arrangedBounds` before
changing rendering code.

## Input or focus does not arrive

Check, in order:

1. element visibility;
2. `interaction.enabled`;
3. `interaction.hitTestVisible`;
4. inherited scroll/popup clips;
5. reverse paint order;
6. modal popup/dialog blocking;
7. focusable flag and tab index;
8. whether an earlier routed phase set `handled`.

Decorative child text should normally be hit-test invisible so the button/list
item remains the target.

## Clipboard or IME failure

Inspect `PlatformCapabilityFlags`. Clipboard and IME operations are optional
backend capabilities. IME candidate rectangles use physical pixels and must be
refreshed after DPI/layout changes.

Composition updates do not immediately mutate the bound value. If a validation
error rejects the commit, the transient candidate remains/cancels according to
the editing transaction rather than partially updating the model.

## Image decode or upload

`PortablePixmapImageDecoder` supports P3/P6 PPM. Supply an `IImageDecoder` for
PNG/JPEG/WebP or proprietary formats.

For a resource stuck in `Loading`, ensure the owner still exists and the worker
is not blocked in a custom decoder. Call `Wait()` only outside the UI frame
loop. On `Failed`, inspect `ImageResource::Error()`.

If decode succeeds but no image paints:

- keep `ImageTextureCache` alive;
- initialize its renderer first;
- call `OnDeviceRestored()` after device recreation;
- invalidate the window after asynchronous completion;
- give the `Image` non-zero arranged bounds;
- verify fit/alignment does not place it entirely outside an unclipped parent.

## Backend contract mismatch

Application creation rejects incompatible contract versions and absent required
capabilities. Rebuild all UI/backend packages from one source revision; do not
silence the check or reinterpret handle layouts.

Use the focused
[`ContractTests.cpp`](../../Packages/NGIN.UI/tests/ContractTests.cpp) behavior
as the compatibility reference.
