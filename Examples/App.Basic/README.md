# App.Basic

`App.Basic` is the canonical first application example in the workspace. It is small enough to read in one pass, but it is still a real project: it owns its project manifest, source root, config, executable output, and runtime startup shape.

This README exists to show the normal V2 path before the richer examples add overlays and extra moving parts.

## What This Example Demonstrates

`App.Basic` is intentionally plain:

- one project-owned executable entrypoint
- no handwritten project `CMakeLists.txt`
- a generated-mode CMake build driven from `.nginproj`
- one default runtime configuration
- one additional diagnostics-oriented configuration
- one config file consumed through `NGIN.Core`
- one project-owned runtime module enabled from the manifest

That makes it the easiest place to see the baseline authored split without unrelated complexity.

## Why It Is The Canonical First Example

Most questions about the V2 model can be answered by this one project:

- what does a project own
- where does the executable boundary live
- how do packages get referenced
- where does runtime metadata go when the app owns it
- how does a configuration change the launch shape without becoming a separate project

If you understand `App.Basic`, the richer examples mostly become “the same model with more overlays.”

## Manifest and Runtime Shape

The project manifest at [App.Basic.nginproj](/home/berggrenmille/NGIN/Examples/App.Basic/App.Basic.nginproj) defines:

- source roots under `src/`
- one executable `Output`
- a generated CMake build section
- a package reference to `NGIN.Core`
- config sources under `config/`
- one project-owned runtime module
- two configurations: `Runtime` and `Diagnostics`

The important part is that both configurations are still the same project. `Diagnostics` adds extra package/plugin behavior, but it does not become a separate executable identity.

If you want to inspect the code side of that shape, start with:

- [main.cpp](/home/berggrenmille/NGIN/Examples/App.Basic/src/main.cpp)
- [app.cfg](/home/berggrenmille/NGIN/Examples/App.Basic/config/app.cfg)

## Validate, Build, and Run

Validate the default runtime configuration:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime
```

Inspect the resolved graph:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime
```

Build a staged output:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic
```

Run it from the staged launch:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Basic/App.Basic.nginproj \
  --configuration Runtime \
  --output build/manual/App.Basic
```

If you want to compare the alternate setup, run the same flow with `--configuration Diagnostics`.

## What To Inspect Next

After `App.Basic`, the natural next step is [App.Showcase](/home/berggrenmille/NGIN/Examples/App.Showcase/README.md). That example keeps the same core model, but makes configuration-level overlays and advanced runtime metadata much more visible.
