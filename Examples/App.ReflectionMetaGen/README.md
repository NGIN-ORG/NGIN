# App.ReflectionMetaGen

`App.ReflectionMetaGen` is a focused example for the first `ngin metagen`
workflow. It uses NGIN annotation macros in normal C++ headers and opts into
MetaGen from the project manifest. `ngin build` generates and compiles the
reflection metadata automatically.

## Build And Run The App

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli

./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.ReflectionMetaGen/App.ReflectionMetaGen.nginproj \
  --profile Runtime

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.ReflectionMetaGen/App.ReflectionMetaGen.nginproj \
  --profile Runtime
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
.ngin/metagen/App.ReflectionMetaGen/Runtime/App.ReflectionMetaGen.reflection.generated.cpp
```
