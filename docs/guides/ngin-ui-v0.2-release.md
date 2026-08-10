# NGIN.UI 0.2 Release Notes

Version 0.2 turns the first public UI surface into a more dependable desktop
toolkit. It adds bounded text and image storage, packaged international fonts,
PNG and JPEG images, Windows UI Automation, Grid, WrapPanel, Canvas,
virtualized lists, and customizable awaitable motion.

## Package versions

The `NGIN.UI`, `NGIN.UI.Backend.SDL3`, `NGIN.UI.Hosting`, and
`NGIN.UI.Accessibility.Windows` packages are version `0.2.0`. Applications
using the 0.2 family should request `>=0.2.0 <0.3.0` for these packages. NGIN
platform, NGIN.Base, NGIN.Core, and SDL3 versions are independent.

The header version is also `0.2.0` through `NGIN_UI_VERSION_*` and
`NGIN::UI::Version*`.

## Moving from 0.1

Most 0.1 composition and control code continues to work. Motion timing is the
accepted breaking change:

- replace `Easing::EaseOut` with `EasingCurve::EaseOut()` (and use the matching
  factory for the other built-in curves);
- replace `AnimationSpec{.duration = d, .easing = curve}` with
  `AnimationSpec{.timing = TweenTiming{.duration = d, .curve = curve}}`;
- use `SpringTiming` for spring motion;
- specialize `AnimationInterpolator<T>` and declare an
  `AnimationProperty<T>` for application-owned animated values;
- use `MotionController` when code must await, cancel, or sequence motion.

There is no legacy motion engine or silent compatibility path. The
[motion guide](ngin-ui-motion.md) contains complete examples and lifetime
rules.

`ImageTextureCache` now has a fixed entry and RGBA8 memory budget. Its default
constructor remains valid. Pass `ImageTextureCacheOptions` only when an
application needs a different measured limit.

## Published release budgets

All time limits are Release-build medians. Allocation counts cover ordinary
heap allocations in the measured path.

| Area | Workload | Time | Allocations or storage |
|---|---:|---:|---:|
| Composition | 2,000 nodes | 250 ms | 20,000 allocations |
| Layout | 2,000 retained nodes | 250 ms | 2,000 allocations |
| Text | Shaped paragraph | 500 ms | 20,000 allocations |
| Ordinary list | 10,000 items | 2,000 ms | 450,000 allocations |
| Virtualized list | 100,000 logical items | 50 ms | 5,000 allocations and at most 40 live nodes |
| Glyph atlas | Normal runtime | — | Four 1024×1024 R8 pages (4 MiB) |
| Image cache | Normal runtime | — | 128 textures and 256 MiB RGBA8 |
| One decoded image | Decode boundary | — | 16,384×16,384 maximum, 256 MiB decoded output |
| Encoded image | Decode boundary | — | 64 MiB |
| Staged fonts | Gallery/runtime assets | — | 4,126,900 bytes |

The image cache removes expired entries first and then the least recently used
texture. An image larger than the configured byte limit returns a resource
error. `ImageCacheDiagnostics` reports the configured limits, current and peak
occupancy, evictions, and capacity failures. Glyph, image, and realization
figures are also visible on the Gallery Diagnostics and Collections pages.

## Build and publish the demo

Build all three shared-view products from their authored manifests:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --configuration Release --output build/release/NGIN.UI.Gallery-0.2.0

build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery.Hosted/NGIN.UI.Gallery.Hosted.nginproj `
  --configuration Release --output build/release/NGIN.UI.Gallery.Hosted-0.2.0

build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery.Tests/NGIN.UI.Gallery.Tests.nginproj `
  --configuration Release --output build/release/NGIN.UI.Gallery.Tests-0.2.0
```

Create the standalone archive:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe publish demo `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --configuration Release --output build/release/NGIN.UI.Gallery-0.2.0
```

The result is `dist/NGIN.UI.Gallery-0.2.0-demo.zip`. It contains the executable,
runtime libraries, image, fonts, licenses, notices, and a short run guide. It
can be extracted and run without the source tree.

## Supported checks

The release gates run core tests, visual checks, budgets, public documentation,
the installed CMake consumer, and standalone, hosted, and headless Gallery
paths on Windows, Linux, and macOS. Windows additionally runs the native UI
Automation provider test. The manual Narrator checklist remains an interactive
release check because assistive-technology behavior cannot be fully judged by
a headless test.
