# App.Basic

`App.Basic` is a compact hosted application example. It is still small enough to
read in one pass, but it shows more of the authored model than
`App.HostedCore`: the manifest owns source roots, output, config, package
references, profiles, and project-owned runtime metadata.

Start here after `App.NativeMinimal` and `App.HostedCore`.

## What It Demonstrates

- one project-owned executable entry point
- no handwritten project `CMakeLists.txt`
- generated-mode CMake backend input driven from `.nginproj`
- one package reference to `NGIN.Core`
- one config file consumed through `NGIN.Core`
- one project-owned runtime module declared and enabled from the manifest
- two profiles: `Runtime` and `Diagnostics`

The important modeling point is that both profiles are still the same
project. `Diagnostics` adds extra package/plugin behavior, but it does not
become a separate executable identity.

## Files To Read

- [`App.Basic.nginproj`](App.Basic.nginproj) defines the project shape.
- [`src/main.cpp`](src/main.cpp) contains the executable entry point.
- [`config/app.cfg`](config/app.cfg) is staged into the output.

## Validate, Graph, Build, and Run

From the repository root, build the CLI first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Validate the default runtime profile:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.Basic/App.Basic.nginproj \
  --profile Runtime
```

Inspect the resolved graph:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.Basic/App.Basic.nginproj \
  --profile Runtime
```

Build a staged output:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.Basic/App.Basic.nginproj \
  --profile Runtime \
  --output build/manual/App.Basic
```

Run from the staged launch:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.Basic/App.Basic.nginproj \
  --profile Runtime \
  --output build/manual/App.Basic
```

To compare the alternate setup, run the same flow with
`--profile Diagnostics` and a separate output directory such as
`build/manual/App.Basic.Diagnostics`.

## What To Inspect Next

After `App.Basic`, read
[`../App.Showcase/README.md`](../App.Showcase/README.md). It keeps the same
core model, then adds richer profile overlays and advanced runtime
metadata.
