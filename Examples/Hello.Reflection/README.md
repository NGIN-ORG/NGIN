# Hello.Reflection

This example opts into the `NGIN.Reflection.MetaGen` package feature. The
generator reads annotated C++ headers, emits reflection source, and compiles it
into the application.

Read [`Hello.Reflection.nginproj`](Hello.Reflection.nginproj),
[`src/Player.hpp`](src/Player.hpp), and [`src/main.cpp`](src/main.cpp).

```bash
cmake --build build/dev --target ngin_reflection_metagen
ngin package lock --project Examples/Hello.Reflection/Hello.Reflection.nginproj --configuration Debug --output build/manual/Hello.Reflection/ngin.lock
ngin build --project Examples/Hello.Reflection/Hello.Reflection.nginproj --configuration Debug --lock build/manual/Hello.Reflection/ngin.lock --output build/manual/Hello.Reflection
ngin run --project Examples/Hello.Reflection/Hello.Reflection.nginproj --configuration Debug --lock build/manual/Hello.Reflection/ngin.lock --output build/manual/Hello.Reflection
```

MetaGen uses the target's selected compiler for preprocessing and does not
require LLVM or libclang. Generated descriptors, the module aggregate,
`ReflectionModel` JSON, and its ownership manifest remain under the selected
build output and should not be edited. The workspace requires the generator's
host PackageInstance to match an explicit dependency lock before the Action can
execute. See the [MetaGen guide](../../docs/guides/reflection-metagen.md) for the
annotation contract and supported syntax.
