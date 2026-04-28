# App.Showcase

`App.Showcase` is the richer application example for the active NGIN model. It
exists to keep `App.Basic` small while still showing what happens when one
project has several meaningful profiles, profile-level overlays, and
project-owned runtime metadata that affects startup behavior.

## What It Demonstrates

- several profiles in one project
- profile-level config overlays
- environment-level package overlays
- project-owned runtime modules enabled and disabled by profile
- host-profile changes between console and service-style setups
- reflection-gated composition

It is still one project. The point is to show how much variation can live inside
one buildable unit before it should be split into another project.

## Important Profiles

[`App.Showcase.nginproj`](App.Showcase.nginproj) defines these profiles:

- `Runtime`: baseline console-oriented runtime shape.
- `Runtime.DevTools`: baseline runtime plus developer-tools module.
- `Runtime.Diagnostics`: diagnostics environment, diagnostics config, and
  diagnostics module.
- `Runtime.Reflection`: reflection-enabled composition plus the reflection
  package environment.
- `Service`: staging-oriented environment with service module enabled and the
  console banner disabled.

Those are all profiles of `App.Showcase`. They differ in launch and
runtime composition, not in whether the project itself is a separate executable
identity.

## Files To Read

- [`App.Showcase.nginproj`](App.Showcase.nginproj) for profile overlays.
- [`src/main.cpp`](src/main.cpp) for runtime checks and module registration.
- `config/profiles/` for per-profile config overlays.

## Validate, Graph, Build, and Run

From the repository root, build the CLI first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Validate a specific profile:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --profile Runtime.Reflection
```

Inspect the resolved graph for another profile:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --profile Runtime.Diagnostics
```

Build and run the developer-tools profile:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --profile Runtime.DevTools \
  --output build/manual/App.Showcase.DevTools

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --profile Runtime.DevTools \
  --output build/manual/App.Showcase.DevTools
```

Build the service-style profile separately:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --profile Service \
  --output build/manual/App.Showcase.Service
```

## What To Inspect Next

After this, inspect `Examples/Game.Engine`, `Examples/Game.Client`, and
`Examples/Game.Server`. They show the opposite modeling decision: separate
buildable outputs should usually be separate projects, not profiles of one
project.
