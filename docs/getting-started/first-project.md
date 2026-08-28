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
<Executable Name="Hello">
  <Build><Source Include="src/**/*.cpp" /></Build>
</Executable>
```

## Validate, build, and run

From the `Hello` directory:

```bash
ngin validate
ngin build
ngin run
```

NGIN discovers `Hello.nginproj`, resolves the selected configuration, target,
and toolchain, generates a CMake build, compiles the executable, and creates a
staged runnable directory.

Use `ngin graph --format json` to see the resolved Composition Graph and
`ngin inspect --effective` to see the normalized Manifest IR, including the
implicit default Run and built-in selection facts.

## Create a project from the CLI

The CLI creates either physical product kind:

```bash
ngin new executable Hello
ngin new library Math
```

## Next steps

- Learn how [projects](../guides/projects.md) are structured.
- Add workspace [selection and profiles](../reference/workspace-manifest.md).
- Consume a [package](../guides/packages.md).
- Compare your project with [Hello.Native](../../Examples/Hello.Native).
