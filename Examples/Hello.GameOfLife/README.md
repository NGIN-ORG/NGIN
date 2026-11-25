# Hello.GameOfLife

An interactive Conway simulation combining `NGIN.ECS` and `NGIN.UI`. The ECS
updates a 1024 by 1024 universe while the UI provides controls, diagnostics,
pan and zoom, painting, a minimap, and texture-backed presentation.

```bash
ngin build --project Examples/Hello.GameOfLife/Hello.GameOfLife.nginproj --configuration Debug --output build/manual/Hello.GameOfLife
ngin run --project Examples/Hello.GameOfLife/Hello.GameOfLife.nginproj --configuration Debug --output build/manual/Hello.GameOfLife
```

The window mounts `MainView` once. The board is a custom-painted view backed by
one dynamic texture. It keeps keyboard focus, zoom and pan while the observable
controls and statistics update. The simulation repaints completed batches and
publishes statistics at most ten times per second while running.

Pass `--smoke` after `--` to render the board, advance two generations and
check the updated statistics, keyboard focus and display-list size.

The example is intentionally larger than the learning samples. Start with
[Hello.ECS](../Hello.ECS) and the [NGIN.UI Gallery](../NGIN.UI.Gallery) if you
want to learn either subsystem in isolation.
