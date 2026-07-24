# NGIN.UI Gallery

This is the standalone native control catalogue for `NGIN.UI`. It uses only
public APIs from `NGIN.UI` and `NGIN.UI.Backend.SDL3`.

![NGIN.UI gallery Inputs page](docs/gallery-overview.png)

The sidebar exposes nine focused pages:

- **Overview** introduces the architecture, theme palette, retained state, and
  public custom badge, progress-ring, and chart examples;
- **Layout** demonstrates rows, columns, overlays, flex sizing, and scrolling;
- **Typography** shows native HarfBuzz shaping, FreeType rasterization, UTF-8,
  Arabic text, and grapheme clusters;
- **Inputs** presents buttons, check boxes, typed radio groups, switches,
  sliders, progress bars, labels, delayed tooltips, and editable, password,
  read-only, invalid, and disabled text fields;
- **Collections** records the current keyed scrolling foundation and the
  selection controls planned next;
- **Overlays** demonstrates modal popups and dismissal;
- **Windows** opens independent windows and owner-modal dialogs;
- **Resources** switches the light/dark theme and shows typed resource scopes;
- **Diagnostics** exposes live frame statistics, semantics, and the inspector
  overlay.

Together, the pages exercise:

- SDL3 window, event-loop, resize, and SDL_GPU-backed rendering;
- retained composition, responsive layout, semantics, and input;
- HarfBuzz shaping, FreeType glyph rasterization, and the bundled Noto Sans
  font;
- grapheme-aware text editing, selection, clipboard commands, and IME;
- themed borders, rounded control chrome, focus visuals, visual scrollbars,
  popups, and multiple windows.

Its model and `ComposeMainView()` are shared unchanged with
[`../NGIN.UI.Gallery.Hosted`](../NGIN.UI.Gallery.Hosted/), which runs the same
view through `NGIN.Core`.

Build and launch it through the V4 product manifest:

```sh
ngin build --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj \
  --profile Debug --output build/manual/NGIN.UI.Gallery
```

Run the staged executable without arguments to use the gallery interactively:

```powershell
build/manual/NGIN.UI.Gallery/bin/NGIN.UI.Gallery.exe
```

Pass `--smoke` to visit and render every catalogue page, then exit
automatically. Without that flag, the gallery uses the event-driven application
loop until its window is closed.

The deterministic headless companion product is
[`../NGIN.UI.Gallery.Tests`](../NGIN.UI.Gallery.Tests/). It renders every page
through the recording backend and also checks theme, popup, inspector,
auxiliary-window, and modal-dialog state.
