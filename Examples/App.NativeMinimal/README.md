# App.NativeMinimal

`App.NativeMinimal` is the smallest plain native application example. It proves
that NGIN tooling can validate, build, stage, and run a C++ executable without
linking `NGIN.Core` and without using the hosted runtime.

The project is just normal C++ source plus an `.nginproj` file:

- [`App.NativeMinimal.nginproj`](App.NativeMinimal.nginproj)
- [`src/main.cpp`](src/main.cpp)

## What It Demonstrates

- one project-owned executable
- generated-mode CMake backend input
- no package references
- no `NGIN.Core`
- one `Runtime` profile
- one named condition used by a conditional compile definition
- one generated staged output directory

This is the first example to read if you want to separate the NGIN tooling layer
from the optional hosted runtime.

The manifest includes a small `LocalDebug` condition:

```xml
<Conditions>
  <Condition Name="LocalDebug"
             Environment="local"
             BuildType="Debug" />
</Conditions>
```

The generated build uses that condition through `Condition`:

```xml
<CompileDefinitions>
  <Definition Value="APP_NATIVE_MINIMAL_LOCAL_DEBUG"
              Visibility="Private"
              Condition="LocalDebug" />
</CompileDefinitions>
```

This demonstrates reusable manifest selection without changing the program's
runtime behavior.

## Validate, Graph, Build, and Run

From the repository root, build the CLI first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

Validate the project:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime
```

Inspect the resolved graph:

```bash
./build/dev/Tools/NGIN.CLI/ngin graph \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime
```

Build a staged output:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime \
  --output build/manual/App.NativeMinimal
```

Run from the staged launch:

```bash
./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.NativeMinimal/App.NativeMinimal.nginproj \
  --profile Runtime \
  --output build/manual/App.NativeMinimal
```

## What To Inspect Next

After this, read [`../App.HostedCore/README.md`](../App.HostedCore/README.md).
It keeps the same build/stage/run model, then adds the optional `NGIN.Core`
hosted runtime.
