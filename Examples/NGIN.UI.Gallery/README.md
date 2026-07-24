# NGIN.UI Gallery

This is the standalone native smoke product for `NGIN.UI`. It uses only public
APIs from `NGIN.UI` and `NGIN.UI.Backend.SDL3` and exercises:

- SDL3 window, event-loop, resize, and SDL_GPU-backed rendering;
- retained composition, layout, semantics, and button input;
- HarfBuzz shaping, FreeType glyph rasterization, and the bundled Noto Sans
  font;
- grapheme-aware text editing, selection, clipboard commands, and IME.

Build and launch it through the V4 product manifest:

```sh
ngin build --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj \
  --profile Debug --output build/manual/NGIN.UI.Gallery
```

Pass `--smoke` to render three native frames and exit automatically. Without
that flag, the gallery uses the event-driven application loop until its window
is closed.
