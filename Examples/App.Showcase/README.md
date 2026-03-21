# App.Showcase

`App.Showcase` is the richer example project for the active NGIN model.

It keeps `App.Basic` small and demonstrates:

- multiple variants in a single project
- variant-level config overlays
- variant-level package refs
- project-owned runtime modules enabled and disabled per variant
- host-profile changes between console and service variants
- reflection-gated composition

Validate a specific variant:

```bash
./build/dev/Tools/NGIN.CLI/ngin project validate \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --variant Runtime.Reflection
```

Inspect the resolved graph:

```bash
./build/dev/Tools/NGIN.CLI/ngin project graph \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --variant Runtime.Diagnostics
```

Build a staged layout:

```bash
./build/dev/Tools/NGIN.CLI/ngin project build \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --variant Service \
  --output build/manual/App.Showcase
```

The executable accepts a runtime variant override so the same binary can demonstrate each authored variant:

```bash
./build/manual/App.Showcase/bin/App.Showcase --variant Runtime.DevTools
APP_SHOWCASE_VARIANT=Service ./build/manual/App.Showcase/bin/App.Showcase
```
