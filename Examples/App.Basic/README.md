# App.Basic

`App.Basic` is the smallest real application example in the workspace.

It is intentionally generic:

- one project
- one project-owned executable entrypoint
- no handwritten project `CMakeLists.txt`
- one variant that builds and stages through `ngin`
- one config file consumed through `NGIN.Core`

If you want a broader example that shows multiple variants, variant-level package refs, config overlays, and project-owned runtime module toggles, use `Examples/App.Showcase/`.

Use it to exercise the normal app flow:

```bash
./build/dev/Tools/NGIN.CLI/ngin project validate \
  --project Examples/App.Basic/App.Basic.nginproj \
  --variant Runtime

./build/dev/Tools/NGIN.CLI/ngin project build \
  --project Examples/App.Basic/App.Basic.nginproj \
  --variant Runtime \
  --output build/manual/App.Basic
```
