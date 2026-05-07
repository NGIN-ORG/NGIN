# Hello.Native

`Hello.Native` is the smallest CLI-managed C++ executable in the repository.
It does not link `NGIN.Core`.

## What This Proves

- NGIN can validate a `.nginproj`.
- NGIN can generate CMake backend input.
- NGIN can build and stage a normal native executable.
- The selected `Debug` profile drives conditional build metadata.

## Files To Read

- [`Hello.Native.nginproj`](Hello.Native.nginproj)
- [`src/main.cpp`](src/main.cpp)

## Build And Run

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli

./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug

./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug \
  --output build/manual/Hello.Native

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --profile Debug \
  --output build/manual/Hello.Native
```

Expected output:

```text
Hello.Native running
```

## What This Does Not Show

This example intentionally avoids packages, project references, runtime modules,
plugins, and `NGIN.Core`.
