# Hello.Reflection

This example opts into the `NGIN.Reflection.MetaGen` package feature. The
generator reads annotated C++ headers, emits reflection source, and compiles it
into the application.

Read [`Hello.Reflection.nginproj`](Hello.Reflection.nginproj),
[`src/Player.hpp`](src/Player.hpp), and [`src/main.cpp`](src/main.cpp).

```bash
cmake --build build/dev --target ngin_reflection_metagen
ngin build --project Examples/Hello.Reflection/Hello.Reflection.nginproj --profile Debug --output build/manual/Hello.Reflection
ngin run --project Examples/Hello.Reflection/Hello.Reflection.nginproj --profile Debug --output build/manual/Hello.Reflection
```

LLVM and libclang are required for MetaGen. Generated files remain under the
selected build output and should not be edited.
