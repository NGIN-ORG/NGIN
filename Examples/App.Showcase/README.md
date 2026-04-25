# App.Showcase

`App.Showcase` is the richer application example for the active NGIN model. It
exists to keep `App.Basic` small while still showing what happens when one
project has several meaningful configurations, configuration-level overlays, and
project-owned runtime metadata that affects startup behavior.

## What It Demonstrates

- several configurations in one project
- configuration-level config overlays
- environment-level package overlays
- project-owned runtime modules enabled and disabled by configuration
- host-profile changes between console and service-style setups
- reflection-gated composition

It is still one project. The point is to show how much variation can live inside
one buildable unit before it should be split into another project.

## Important Configurations

[`App.Showcase.nginproj`](App.Showcase.nginproj) defines these configurations:

- `Runtime`: baseline console-oriented runtime shape.
- `Runtime.DevTools`: baseline runtime plus developer-tools module.
- `Runtime.Diagnostics`: diagnostics environment, diagnostics config, and
  diagnostics module.
- `Runtime.Reflection`: reflection-enabled composition plus the reflection
  package environment.
- `Service`: staging-oriented environment with service module enabled and the
  console banner disabled.

Those are all configurations of `App.Showcase`. They differ in launch and
runtime composition, not in whether the project itself is a separate executable
identity.

## Files To Read

- [`App.Showcase.nginproj`](App.Showcase.nginproj) for configuration overlays.
- [`src/main.cpp`](src/main.cpp) for runtime checks and module registration.
- `config/configurations/` for per-configuration config overlays.

## Validate, Graph, Build, and Run

From the repository root, build the CLI first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Validate a specific configuration:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.Reflection
```

Inspect the resolved graph for another configuration:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.Diagnostics
```

Build and run the developer-tools configuration:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.DevTools \
  --output build/manual/App.Showcase.DevTools

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.DevTools \
  --output build/manual/App.Showcase.DevTools
```

Build the service-style configuration separately:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Service \
  --output build/manual/App.Showcase.Service
```

## What To Inspect Next

After this, inspect `Examples/Game.Engine`, `Examples/Game.Client`, and
`Examples/Game.Server`. They show the opposite modeling decision: separate
buildable outputs should usually be separate projects, not configurations of one
project.
