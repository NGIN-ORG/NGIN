# Hello.Reflection

`Hello.Reflection` is the focused reflection/code-generation example. It uses
NGIN annotation macros in normal C++ headers and opts into the
`NGIN.Reflection.MetaGen` package feature.

## What This Proves

- Package-provided command generators run during `ngin build`.
- MetaGen reads the generated context from the CLI.
- Generated reflection source is compiled into the executable.
- Runtime reflection lookup, construction, field access, and property writes
  work against the generated metadata.

## Files To Read

- [`Hello.Reflection.nginproj`](Hello.Reflection.nginproj)
- [`src/Player.hpp`](src/Player.hpp)
- [`src/main.cpp`](src/main.cpp)

## Build And Run

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
cmake --build build/dev --target ngin_reflection_metagen

./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Reflection/Hello.Reflection.nginproj \
  --profile Debug \
  --output build/manual/Hello.Reflection

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Reflection/Hello.Reflection.nginproj \
  --profile Debug \
  --output build/manual/Hello.Reflection
```

Expected output:

```text
Reflected type: Demo::Player
fields=1
properties=1
methods=0
display_name=Ada
score=70
updated_score=84
```

The generated source is a build artifact under:

```text
build/manual/Hello.Reflection/.ngin/generated/Hello.Reflection/Debug/reflection/Hello.Reflection.reflection.generated.cpp
```
