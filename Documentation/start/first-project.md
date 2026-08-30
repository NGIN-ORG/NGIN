---
title: Your first project
description: Create, build, and run the smallest NGIN-managed C++ executable.
---

# Your first project

This walkthrough creates one executable with one source file. It uses the NGIN
project system without linking an NGIN library.

## Create the files

```text
Hello/
├── Hello.nginproj
└── src/
    └── main.cpp
```

Create `Hello.nginproj`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="Hello">
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

Create `src/main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello from NGIN!\n";
    return 0;
}
```

## Validate before building

From the `Hello` directory:

```bash
ngin validate --project Hello.nginproj --configuration Debug
```

Validation parses the manifest and resolves the product without invoking the
compiler. Fix validation errors before investigating build output.

## Build and run

```bash
ngin build --project Hello.nginproj --configuration Debug
ngin run --project Hello.nginproj --configuration Debug
```

Executables have an implicit default Run. You only need authored Run metadata
when the application requires arguments, environment, a working directory, or
multiple named launch intents.

## Inspect what NGIN resolved

```bash
ngin graph --project Hello.nginproj --configuration Debug
ngin inspect --project Hello.nginproj --configuration Debug --format json
```

The Composition Graph is the resolved model used by build, stage, run, tests,
publishing, and editor tooling. When behavior is surprising, inspect the graph
before reading generated CMake.

## Add capabilities deliberately

Continue with:

- [Projects](../project-system/projects.md) for Build, Stage, Run, Test, and
  Benchmark behavior.
- [Packages](../project-system/packages.md) to consume a library or tool.
- [Workspaces](../project-system/workspaces.md) for shared discovery, versions,
  profiles, and policy.
- [Libraries](../libraries.md) when the application needs an NGIN library.
