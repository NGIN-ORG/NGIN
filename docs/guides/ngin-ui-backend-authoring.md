# NGIN.UI Backend Authoring

NGIN.UI separates application views from platform and renderer providers. A
backend pair may target SDL, a native OS API, a test harness, a remote surface,
or another graphics stack without changing composition code.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) exercises a real backend
pair, while
[`NGIN.UI.Gallery.Tests`](../../Examples/NGIN.UI.Gallery.Tests/) exercises the
same view through deterministic test backends.

Use these buildable references:

- [`NGIN.UI.Backend.SDL3`](../../Packages/NGIN.UI.Backend.SDL3/) for a native
  platform and SDL_GPU implementation;
- [`TestPlatformBackend`](../../Packages/NGIN.UI/src/NGIN/UI/Testing/TestPlatformBackend.cpp)
  for a deterministic platform implementation;
- [`RecordingRenderBackend`](../../Packages/NGIN.UI/src/NGIN/UI/Testing/RecordingRenderBackend.cpp)
  for a deterministic renderer implementation;
- [`ContractTests.cpp`](../../Packages/NGIN.UI/tests/ContractTests.cpp) for
  executable contract examples.

## Shared contract rules

Both backend interfaces publish a `BackendContractVersion` and capability
flags. `CreateApplication()` validates required operations before creating
windows. A backend must:

- return structured `UIError` values instead of throwing through the ABI;
- reject stale or foreign generational handles;
- make destroy operations deterministic;
- keep logical UI units separate from integer pixels;
- document thread affinity and call every event sink on the UI thread;
- avoid retaining spans passed to a call after that call returns;
- report native error codes and operation names when available.

Provider packages should expose their factories without leaking implementation
headers:

```cpp
std::unique_ptr<IPlatformBackend> CreatePlatformBackend();
std::unique_ptr<IRenderBackend> CreateRendererBackend();
```

## Platform backend

Implement `IPlatformBackend` from `Platform.hpp`.

### Initialization and capabilities

`Initialize()` runs before window creation. Publish only capabilities the
backend actually implements:

- clipboard;
- IME/text input;
- cursors;
- multiple windows;
- dialogs/file drop as represented by the current flags;
- reduced-motion preference when the provider can read it.

If a feature is absent, leave its flag clear and return `Unsupported` from a
direct request. Controls use capability negotiation and surface errors through
their callbacks.

### Window lifecycle

`CreateWindow()` returns a new generational `PlatformWindowHandle`.
`DestroyWindow()` invalidates it and releases native ownership. Map:

- authored initial size to physical pixels;
- native resize/move/focus/close notifications to normalized events;
- content scale changes to `WindowScaleChanged`;
- owner/modal relations without reparenting unrelated windows.

Do not synthesize a close on `WindowCloseRequested`; the application decides
whether to call `CloseWindow()`.

### Event pumping

`PumpEvents()` or the backend's wait operation must translate native events to
the `IPlatformEventSink` set by NGIN.UI. Preserve ordering within one platform
queue.

Pointer positions and wheel deltas are converted to window-local
device-independent units. Keep stable pointer IDs from press through release.
Normalize keys into both physical and logical values and set modifier flags
independently.

Hosted backends must provide a wake mechanism so `IUIDispatcher::Post()` and
Core stop requests interrupt the event wait.

`MonotonicNow()` must use a clock that never moves backward and must share the
same time domain as event waiting. NGIN.UI uses it for scheduled work and
animation deadlines. `ReducedMotionEnabled()` returns the current platform
preference; advertise `ReducedMotionPreference` only when that value comes
from a real platform setting. A preference change must wake the event loop so
active motion settles on the next pump.

### Clipboard and IME

Clipboard text is UTF-8. IME start/stop calls are paired with focus transitions.
The candidate rectangle is in physical pixels and should be updated when
focus, layout, or scale changes.

Composition updates use UTF-8 byte offsets. Commit text arrives as
`TextInput`. Do not convert those offsets to UTF-16 indices in the normalized
event.

## Renderer backend

Implement `IRenderBackend` from `Rendering.hpp`.

### Device and surfaces

`Initialize()` creates or selects the device. `CreateSurface()` binds a live
platform window and pixel extent. Resize and destroy must wait or synchronize
as required by the graphics API without exposing stale surface handles.

`WaitIdle()` is a correctness boundary for shutdown and destructive device
transitions; it should not be inserted into every frame.

### Textures

Support `R8`, `RGBA8`, and `BGRA8` according to advertised capabilities.
`UpdateTexture()` receives a byte span valid only for the call and an explicit
row stride.

Honor the `TextureFilter` selected in `TextureCreateInfo`. `Nearest` must select
one source texel without blending adjacent texels; `Linear` must interpolate
adjacent texels. Native glyph atlases use nearest filtering because FreeType
already rasterizes their coverage at the target device scale. Images use linear
filtering.

R8 glyph atlases are sampled as alpha. RGBA/BGRA images use premultiplied-alpha
blending at draw time. Backend texture handles remain opaque to logical
`ImageResource`; `ImageTextureCache` is the upload owner.

On device loss:

1. reject or stop new frame submission;
2. release surfaces and textures;
3. notify renderer-bound caches (`OnDeviceLost()`);
4. recreate the device/surfaces;
5. call cache restoration hooks;
6. invalidate every window for repaint.

### Render packets

`RenderPacket` contains immutable spans for one call:

- vertices in target pixel space;
- 32-bit indices;
- ordered batches with texture, scissor, blend mode, and index range;
- optional texture updates;
- target extent, scale, and clear color.

Respect batch order. Apply each scissor in target pixels. An invalid batch
range, stale texture, or dead surface is `RenderFailed`, not undefined
behavior.

`Present()` follows a successful `Render()` and owns swapchain presentation
semantics. Occlusion/minimization may be a successful no-op when the native API
requires it.

### Shaders

The current SDL3 provider uses `SDL_CreateGPURenderer`; SDL owns its internal
GPU pipelines and no application-visible shader files are staged. A provider
implemented directly on raw `SDL_GPU` or another graphics API must supply
equivalent semantics:

- position transform to clip space;
- packed vertex tint;
- texture sampling;
- R8 glyph alpha handling;
- premultiplied output.

Keep any provider-owned shader artifacts in that provider package and stage
them through its manifest. The backend-neutral core must not know shader paths.

## Package integration

A provider package depends on `NGIN.UI`, exports one library target, and is
selected by the application product:

```xml
<Package SchemaVersion="4"
         Name="Vendor.UI.Backend"
         Version="0.1.0">
  <Build Backend="CMake" Mode="AddSubdirectory" />
  <Uses>
    <Package Name="NGIN.UI"
             Version=">=0.1.0 &lt;0.2.0"
             Scope="Target" />
  </Uses>
  <Library Name="Vendor.UI.Backend">
    <Exports>
      <LibraryTarget Name="Vendor::UIBackend" Linkage="Static" />
    </Exports>
  </Library>
</Package>
```

Do not add a provider dependency to `NGIN.UI`; application products choose
backends at their composition boundary.

## Verification checklist

At minimum, run equivalent tests for:

- contract version and capability negotiation;
- invalid/stale handles;
- two simultaneous windows and resize;
- focus, scale, pointer capture, keyboard, text, clipboard, and IME events;
- monotonic deadline waits and reduced-motion preference changes;
- texture create/update/destroy for every advertised format;
- scissor, blend mode, batch ordering, and resize;
- minimized/occluded presentation;
- shutdown and injected device loss;
- validation-enabled startup.

The SDL3 backend's focused tests and the headless gallery are the current
reference acceptance path.
