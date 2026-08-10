# Hello.Native

The smallest CLI-managed application in the repository. It is ordinary C++ and
does not link `NGIN.Core` or another NGIN library.

Read [`Hello.Native.nginproj`](Hello.Native.nginproj) and
[`src/main.cpp`](src/main.cpp), then run:

```bash
ngin validate --project Examples/Hello.Native/Hello.Native.nginproj --configuration Debug
ngin build --project Examples/Hello.Native/Hello.Native.nginproj --configuration Debug --output build/manual/Hello.Native
ngin run --project Examples/Hello.Native/Hello.Native.nginproj --configuration Debug --output build/manual/Hello.Native
```

Expected application output:

```text
Hello.Native running
```

This is the best starting point for learning project manifests, generated CMake,
staging, and launch behavior.
