# NGIN.UI SDL3 Backend

`NGIN.UI.Backend.SDL3` is the native platform and renderer provider for
`NGIN.UI`. It implements `IPlatformBackend` with SDL3 windows and events, and
implements `IRenderBackend` with SDL's GPU-backed renderer API.

The backend provides:

- high-DPI native windows and multiple-window lifecycle;
- normalized keyboard, text, IME, pointer, wheel, file-drop, display, and theme
  events;
- event-loop polling, bounded waits, and cross-thread wake events;
- a monotonic animation clock and the Windows client-animation preference;
- clipboard, cursor, display, and text-input services;
- SDL_GPU-backed surfaces, indexed geometry, scissor rectangles,
  premultiplied-alpha blending, and texture updates;
- renderer-wide logical textures mirrored lazily into each window surface,
  preserving the backend-neutral multi-window texture contract.

The public factories are declared in
`<NGIN/UI/Backend/SDL3/SDL3.hpp>`. Applications own the returned platform and
renderer instances and pass them to `NGIN::UI::Application`.

## Provider

The sibling [`../SDL3`](../SDL3/) package is the selected source provider. It
pins SDL 3.4.12 and exposes the static `SDL3::SDL3-static` target. No SDL2
compatibility path is included.

For a focused headless contract check:

```sh
cmake -S Packages/NGIN.UI.Backend.SDL3 -B build/ngin-ui-sdl3 \
  -DNGIN_UI_SDL3_BUILD_TESTS=ON
cmake --build build/ngin-ui-sdl3 --target NGINUISDL3Tests
ctest --test-dir build/ngin-ui-sdl3 --output-on-failure
```
