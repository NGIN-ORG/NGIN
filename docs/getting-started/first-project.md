# Your first project

This walkthrough creates a plain C++ executable. It uses the NGIN project tool
but does not link any NGIN library.

## Create the files

```text
Hello/
|-- Hello.nginproj
`-- src/
    `-- main.cpp
```

`src/main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello from NGIN\n";
}
```

`Hello.nginproj`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Hello" DefaultProfile="Debug">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Executable="$(OutputName)" />
  </Application>

  <Profile Name="Debug">
    <Defaults>
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
      <LinkTimeOptimization Enabled="false" />
      <TargetPlatform Name="host" />
    </Defaults>
  </Profile>
</Project>
```

## Validate, build, and run

From the `Hello` directory:

```bash
ngin validate
ngin build
ngin run
```

NGIN discovers `Hello.nginproj`, resolves the `Debug` profile, generates a
CMake build, compiles the executable, and creates a staged runnable directory.

Use `ngin graph` to see the resolved project and `ngin inspect --format json`
for machine-readable output.

## Create a project from the CLI

The CLI can create the starting layout for common product kinds:

```bash
ngin new app Hello
ngin new lib Math
ngin new tool AssetCompiler
```

## Next steps

- Learn how [projects](../guides/projects.md) are structured.
- Add a [profile](../guides/profiles.md).
- Consume a [package](../guides/packages.md).
- Compare your project with [Hello.Native](../../Examples/Hello.Native).
