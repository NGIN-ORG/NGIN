# NGIN.UI

`NGIN.UI` is the backend-neutral core of NGIN's C++23 application UI toolkit.
It depends only on `NGIN.Base`; native windowing, graphics APIs, and optional
`NGIN.Core` hosting belong to separate packages.

The current implementation provides the headless contracts and composition
foundation:

- device-independent and pixel geometry;
- generational platform/render handles;
- structured UI errors and results;
- normalized window, pointer, keyboard, text, drop, and theme events;
- platform and renderer backend contracts;
- application and logical-window lifecycle;
- deterministic headless platform and recording renderer implementations;
- explicit RAII-scoped composition;
- packed generational runtime-node storage;
- static and keyed reconciliation;
- independent composition, layout, and paint invalidation;
- constraint-based measure/arrange for rows, columns, overlays, padding, and
  alignment;
- typed node layout and solid-background properties;
- backend-neutral display-list construction;
- DPI-aware solid-rectangle tessellation and adjacent-batch coalescing.

Build and test directly:

```bash
cmake -S Packages/NGIN.UI -B build/ngin-ui -DNGIN_UI_BUILD_TESTS=ON
cmake --build build/ngin-ui --target NGINUITests
ctest --test-dir build/ngin-ui --output-on-failure
```

The package intentionally has no SDL dependency. The SDL3 + SDL_GPU
implementation will live in `NGIN.UI.Backend.SDL3`.
