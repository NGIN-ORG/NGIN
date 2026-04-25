# App.ReflectionMetaGen

`App.ReflectionMetaGen` is a focused example for the first `ngin metagen`
workflow. It uses NGIN annotation macros in normal C++ source and generates a
reflection metadata `.cpp` file.

The current MetaGen slice generates reflection source for inspection. Generated
files are not wired into `ngin build` yet.

## Generate Reflection Metadata

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli

./build/dev/Tools/NGIN.CLI/ngin metagen \
  --project Examples/App.ReflectionMetaGen/App.ReflectionMetaGen.nginproj \
  --configuration Runtime \
  --output build/manual/App.ReflectionMetaGen/metagen
```

The generated file is:

```text
build/manual/App.ReflectionMetaGen/metagen/App.ReflectionMetaGen.reflection.generated.cpp
```

It should contain generated `NGIN::Reflection::Describe<T>` specializations for
the annotated `Demo::Entity` and `Demo::Player` types.

## Build And Run The App

The app itself is still a normal generated-mode NGIN project:

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/App.ReflectionMetaGen/App.ReflectionMetaGen.nginproj \
  --configuration Runtime \
  --output build/manual/App.ReflectionMetaGen/stage

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/App.ReflectionMetaGen/App.ReflectionMetaGen.nginproj \
  --configuration Runtime \
  --output build/manual/App.ReflectionMetaGen/stage
```

Expected output:

```text
App.ReflectionMetaGen: Ada score=70
```
