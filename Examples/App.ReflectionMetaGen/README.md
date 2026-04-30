# App.ReflectionMetaGen

`App.ReflectionMetaGen` is a focused example for generator-backed reflection
metadata. It uses NGIN annotation macros in normal C++ headers and declares a
feature use for the `NGIN.Reflection.MetaGen` package. `ngin build` runs the
package-provided `ngin-metagen` tool, then compiles the generated reflection
metadata.

## Build And Run The App

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
cmake --build build/dev --target ngin_reflection_metagen

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
.ngin/build/App.ReflectionMetaGen/Runtime/.ngin/generated/App.ReflectionMetaGen/Runtime/reflection/App.ReflectionMetaGen.reflection.generated.cpp
```
