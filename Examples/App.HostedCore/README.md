# App.HostedCore

`App.HostedCore` is the smallest example that uses the optional `NGIN.Core`
hosted runtime. The project is still built and staged by NGIN tooling, but
startup is code-first inside the executable.

The executable does not load its source `.nginproj` at runtime. It starts from
normal C++ code, consumes staged config, and registers the runtime pieces it
needs:

- [`App.HostedCore.nginproj`](App.HostedCore.nginproj)
- [`src/main.cpp`](src/main.cpp)
- [`config/app.cfg`](config/app.cfg)

## What It Demonstrates

- one project-owned executable
- generated-mode CMake backend input
- a package reference to `NGIN.Core`
- staged config through `ConfigSources`
- code-first hosted startup
- no project-owned runtime module metadata in the manifest

This is the recommended shape for applications that want the hosted runtime
model while remaining shippable as a normal native application layout.

## Validate, Graph, Build, and Run

From the repository root, build the CLI first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Validate the project:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.HostedCore/App.HostedCore.nginproj \
  --configuration Runtime
```

Inspect the resolved graph:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.HostedCore/App.HostedCore.nginproj \
  --configuration Runtime
```

Build a staged output:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.HostedCore/App.HostedCore.nginproj \
  --configuration Runtime \
  --output build/manual/App.HostedCore
```

Run from the staged launch:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.HostedCore/App.HostedCore.nginproj \
  --configuration Runtime \
  --output build/manual/App.HostedCore
```

## What To Inspect Next

After this, read [`../App.Basic/README.md`](../App.Basic/README.md). It keeps
the hosted runtime model, then shows how project-owned runtime metadata can be
declared in the manifest.
