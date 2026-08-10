# NGIN.UI Testing and Release Gates

NGIN.UI separates fast structural tests, deterministic pixel tests, application
smoke tests, and performance budgets. The buildable
[`NGIN.UI.Gallery.Tests`](../../Examples/NGIN.UI.Gallery.Tests/) product is the
application-level reference, while
[`SoftwareRendererTests.cpp`](../../Packages/NGIN.UI/tests/SoftwareRendererTests.cpp)
is the pixel-test reference.

## Structural and visual tests

Configure the core test suite once:

```powershell
cmake -S Packages/NGIN.UI -B build/ngin-ui `
  -DNGIN_UI_BUILD_TESTS=ON `
  -DNGIN_UI_BUILD_BENCHMARKS=ON
cmake --build build/ngin-ui --target NGINUITests NGINUIBenchmarks
ctest --test-dir build/ngin-ui --output-on-failure
```

`Testing::RecordingRenderBackend` is best for assertions about render packets,
batches, clips, texture lifetime, and frame counts. It does not produce pixels.

`Testing::SoftwareRenderBackend` is a deterministic CPU implementation of
`IRenderBackend`. Render into a small surface, call `Snapshot()`, and compare
the RGBA8 result with `CompareVisuals()`. `VisualTolerance` independently limits
per-channel drift, the ratio of materially different pixels, and mean absolute
error. Keep tolerances explicit and narrow; a large tolerance turns a golden
image into a weak snapshot test.

Golden files live in `Packages/NGIN.UI/tests/baselines`. Review baseline changes
as visual behavior changes, not as an automatic test-update step.

## Gallery smoke coverage

Build and run the headless product:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery.Tests/NGIN.UI.Gallery.Tests.nginproj `
  --configuration Debug `
  --output build/manual/NGIN.UI.Gallery.Tests

build/manual/NGIN.UI.Gallery.Tests/bin/NGIN.UI.Gallery.Tests.exe
```

The smoke product instantiates every catalogue page and covers resize, pointer
and keyboard activation, clipboard paste, IME pre-edit and commit, popup
dismissal, modal ownership, and multiple-window closure without SDL or a GPU.
The standalone gallery's `--smoke` mode remains the native-backend startup
check.

## Performance and allocation budgets

`NGINUIBenchmarks` reports one JSON object per benchmark and exits nonzero when
a median time or ordinary heap-allocation budget is exceeded:

```powershell
build/ngin-ui/benchmarks/NGINUIBenchmarks.exe
```

The guarded paths are a 2,000-node composition frame, layout of that retained
tree, a shaped paragraph, construction/reconciliation/layout of a
non-virtualized 10,000-item list, and the complete virtualized path for a
100,000-item logical list. The virtualized gate also aborts if more than 40
runtime nodes are live in a 720-pixel viewport. Budgets are in
`Packages/NGIN.UI/benchmarks/Budgets.hpp`. Change a budget only with a measured
architectural reason and record both the old and new baseline in the review.

`NativeTextSystem::AtlasDiagnostics()` reports glyph hits, misses, uploads,
page count and limit, peak pages, pixel-size buckets, used and available pixel
area, evictions, page rebuilds, capacity failures, and device restorations.
`ImageTextureCache` defaults to at most 128 resident textures and 256 MiB of
RGBA8 texture data. It removes expired resources first, then the least recently
used texture. `ImageTextureCache::Diagnostics()` reports the configured limits,
current and peak entries and bytes, hits, misses, uploads, evictions, and
capacity failures. Activity counters are monotonic for the object's lifetime
and are intended for tests and diagnostics, not application behavior. The full
release-budget table is in the
[0.2 release notes](ngin-ui-v0.2-release.md).

The native-text suite forces a small page budget through repeated sizes, holds
live page leases to prove they cannot be recycled, alternates common DPI
scales, and renders real FreeType coverage through the software backend. The
pixel checks cover grayscale antialiasing, centered lines, wrapping,
descenders, and clip edges at 100%, 125%, 150%, and 200%.

## MVVM lifetime budgets

The version 0.3 MVVM contracts have backend-neutral lifetime budgets:

- outstanding ViewModel tasks return to `0` after unmount, window close, or
  application shutdown;
- active subscriptions return to the snapshot baseline after their owner is
  destroyed; `CurrentSubscriptionDiagnostics()` reports active, peak, created,
  and canceled counts;
- one `StateBatch` publishes each changed `State` at most once and recomputes
  each affected `ComputedState` at most once when the outer batch closes;
- the Gallery permits one active load per mounted async screen and one active
  save; both return to zero when canceled or completed;
- unobserved command failures, retained `Composer` instances, and native
  backend dependencies in the install consumer have a budget of zero.

Use delta checks: snapshot before mounting UI and require the active count to
return to that baseline after teardown. Created, canceled, and peak counters
are monotonic diagnostics, not application state.

## Device recreation

Renderer handles are not logical resources. On device loss:

1. stop submitting frames and wait for in-flight work as the backend permits;
2. call `ImageTextureCache::OnDeviceLost()` and
   `NativeTextSystem::OnDeviceLost()`;
3. recreate the renderer/device and surfaces;
4. call `ImageTextureCache::OnDeviceRestored()` with the new renderer and
   `NativeTextSystem::OnDeviceRestored()`;
5. invalidate every window so logical images and glyphs upload lazily.

Use `SetResourcesInvalidatedCallback()` with `Application::InvalidateAll()` for
step 5. The hosting package wires this callback automatically. The image and
native-text tests assert that restored logical resources create new backend
handles and repopulate cache diagnostics.

## Install/export consumption

The release gate installs NGIN.Base and the contracts-only NGIN.UI build into a
prefix, then configures
[`tests/install-consumer`](../../Packages/NGIN.UI/tests/install-consumer/) as an
independent CMake project using `find_package(NGINUI CONFIG REQUIRED)`. This
proves that installed headers, `NGIN::UI`, transitive NGIN.Base discovery, and
the exported CMake config do not rely on source-tree paths.

Package-managed applications normally enable native text through the NGIN
workspace so pinned FreeType and HarfBuzz providers remain part of the resolved
composition. The standalone CMake install gate deliberately uses
`NGIN_UI_ENABLE_NATIVE_TEXT=OFF`; it validates the portable public contracts
without exporting fetched third-party build targets.

The consumer also compiles and runs `State`, `Command`, and ViewModel-lifetime
headers while linking only `NGIN::UI`. This checks that MVVM does not pull SDL,
SDL_GPU, FreeType, HarfBuzz, or another native backend into a backend-neutral
application.

## CI and release assets

`.github/workflows/ui-ci.yml` runs the tests, benchmarks, documentation
coverage check, and install consumer on Windows, Linux, and macOS.

| Gallery product | Windows | Linux | macOS |
|---|---:|---:|---:|
| Standalone native `--smoke` | CI | CI under Xvfb | CI |
| Hosted native `--smoke` | CI | CI under Xvfb | CI |
| Headless checks | CI | CI | CI |

All three products compile the same `Gallery.cpp` and use the same
`GalleryViewModel`; only startup and backend ownership differ.

Applications opt into the `NGIN.UI` `RuntimeAssets` feature and SDL backend
`RuntimeNotices` feature. The resolved stage then contains the bundled font,
OFL text, NGIN.UI dependency notices, and SDL license from authored package
metadata. A release should fail if any declared staged asset is missing.

Source-compatibility guarantees and the deprecation window are defined in the
[compatibility policy](../policies/ngin-ui-source-compatibility.md).
