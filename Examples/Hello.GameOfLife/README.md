# Hello.GameOfLife

An interactive Conway simulation combining `NGIN.ECS` and `NGIN.UI`. The ECS
updates a 1024 by 1024 universe while the UI provides controls, diagnostics,
pan and zoom, painting, a minimap, and texture-backed presentation.

```bash
ngin build --project Examples/Hello.GameOfLife/Hello.GameOfLife.nginproj --profile Debug --output build/manual/Hello.GameOfLife
ngin run --project Examples/Hello.GameOfLife/Hello.GameOfLife.nginproj --profile Debug --output build/manual/Hello.GameOfLife
```

Pass `--smoke` after `--` to compose and render one frame for automation.

The example is intentionally larger than the learning samples. Start with
[Hello.ECS](../Hello.ECS) and the [NGIN.UI Gallery](../NGIN.UI.Gallery) if you
want to learn either subsystem in isolation.
