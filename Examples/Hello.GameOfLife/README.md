# Hello.GameOfLife

`Hello.GameOfLife` combines NGIN.UI and NGIN.ECS in one focused application.
NGIN.ECS evolves a 1024 by 1024 Life universe—1,048,576 entities—while NGIN.UI
presents controls, live ECS/UI diagnostics, and an interactive texture-backed
viewport.

The example demonstrates:

- plain C++ ECS components for cell position, age/trail state, and the next
  generation;
- a fixed-update schedule whose parameter types declare component and resource
  access;
- resource-aware, chunk-parallel evolve and commit systems using the
  deterministic parallel executor;
- batched fixed updates with a chunk-parallel color rasterizer that runs once
  per presentation frame;
- a backend-neutral, revisioned RGBA image resource whose million pixels update
  an existing nearest-filter texture instead of creating per-cell UI geometry;
- a custom element that draws normalized texture regions for instant pan/zoom,
  a live minimap, pointer painting, keyboard input, and semantics;
- age coloring, fading death trails, Conway/HighLife rules, glider fleets,
  random universes, and fields of Acorn methuselahs;
- `State<T>`, bindings, standard controls, layout, theming, and scheduled UI
  work.

The ECS component data is authoritative. A compact board resource provides the
stable spatial snapshot used by the parallel neighbour rule. The commit pass
publishes the next snapshot, maintains population, age, and trails, and the UI
receives one 4 MiB surface revision after a batch rather than one command per
cell.

## Build and run

From the repository root:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/Hello.GameOfLife/Hello.GameOfLife.nginproj `
  --profile Debug `
  --output build/manual/Hello.GameOfLife

build/manual/Hello.GameOfLife/bin/Hello.GameOfLife.exe
```

Run one native composition and render frame for automation:

```powershell
build/manual/Hello.GameOfLife/bin/Hello.GameOfLife.exe --smoke
```

The initial pattern is a fleet of 1,024 gliders. Use Run/Pause and Step to
control the simulation, switch between Conway and HighLife, or seed a random
universe or Acorn field. Primary drag paints cells. The wheel zooms up to 128x,
middle/right drag pans, and clicking the minimap jumps across the universe.
Home resets the camera; arrow keys and Space edit the focused cell. The
logarithmic speed control reports both its exact target and measured throughput.
The sidebar also exposes ECS batch time and the UI's frame time, display-command
count, and draw-batch count so the architecture is visible while it runs.
