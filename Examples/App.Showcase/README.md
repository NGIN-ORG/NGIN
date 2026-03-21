# App.Showcase

`App.Showcase` is the richer example project for the active NGIN model. It exists to keep `App.Basic` small while still showing what happens when one project has several meaningful configurations, configuration-level overlays, and project-owned runtime metadata that affects startup behavior.

This README is here to explain the shape of that example before you start reading the manifest or the code.

## What This Example Demonstrates

`App.Showcase` demonstrates several things that are intentionally absent from `App.Basic`:

- multiple configurations in a single project
- configuration-level config overlays
- configuration-level package references
- project-owned runtime modules enabled and disabled per configuration
- host-profile changes between console and service setups
- reflection-gated composition

It is still one project. The point is to show how much variation can live inside one project before you should split out a second buildable unit.

## Why It Exists Alongside `App.Basic`

`App.Basic` answers “what is the simplest real project shape?”

`App.Showcase` answers the next question: “once I already have that project shape, how do I layer richer startup and environment differences onto it without pretending those are separate applications?”

That is why this example is useful. It makes the configuration model concrete instead of leaving it as a sentence in the docs.

## Important Configurations

The project manifest at [App.Showcase.nginproj](/home/berggrenmille/NGIN/Examples/App.Showcase/App.Showcase.nginproj) currently defines these configurations:

- `Runtime`  
  The baseline console-oriented runtime shape.

- `Runtime.DevTools`  
  Builds on the baseline runtime and enables the developer-tools module.

- `Runtime.Diagnostics`  
  Adds diagnostics-oriented config and the `NGIN.Diagnostics` package.

- `Runtime.Reflection`  
  Enables reflection-aware composition and adds the `NGIN.Reflection` package.

- `Service`  
  Switches the host profile to `Service`, uses a different environment, and disables the console-banner path that only makes sense for the console host.

Those are all still configurations of `App.Showcase`. They differ in launch and runtime composition, not in whether the project itself is a separate executable identity.

## Validate, Graph, Build, and Run

Validate a specific configuration:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.Reflection
```

Inspect the resolved graph for a different configuration:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.Diagnostics
```

Build a staged layout:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Service \
  --output build/manual/App.Showcase
```

Run one of the console-oriented configurations:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Showcase/App.Showcase.nginproj \
  --configuration Runtime.DevTools \
  --output build/manual/App.Showcase
```

## What Advanced Behavior It Exercises

If you want to inspect the interesting parts directly, look at:

- [App.Showcase.nginproj](/home/berggrenmille/NGIN/Examples/App.Showcase/App.Showcase.nginproj) for the authored configuration overlays
- [main.cpp](/home/berggrenmille/NGIN/Examples/App.Showcase/src/main.cpp) for the runtime checks and module registrations
- `config/configurations/` under the example directory for the per-configuration config overlays

This is the example to read when you want to understand where the boundary is between:

- “another configuration of the same project”
- and “this should actually become another project”

If you want that second case after this, the next examples to inspect are `Game.Client` and `Game.Server`.
