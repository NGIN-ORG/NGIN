---
title: NGIN.UI quick start
description: Create a standalone SDL3-backed native window with shaped text and retained content.
---

# NGIN.UI quick start

This guide creates a standalone application using the SDL3 backend. The
workspace must expose the `NGIN.UI` and `NGIN.UI.Backend.SDL3` packages.

## Before you start

You need the NGIN CLI, a C++23 compiler, a desktop environment, and a workspace
that discovers `NGIN.UI` plus a compatible backend provider. Create
`Hello.UI/Hello.UI.nginproj` and `Hello.UI/src/main.cpp`.

## Add the product

```xml
<Executable Name="Hello.UI">
  <Uses>
    <Package Name="NGIN.UI" Version="0.4" />
    <Package Name="NGIN.UI.Backend.SDL3" Version="0.4" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

The selected backend contributes its native runtime dependencies. The product
declares `NGIN.UI` directly because its source consumes the public UI API.

## Create the application and window

```cpp
#include <NGIN/UI/Backend/SDL3/Desktop.hpp>
#include <iostream>

int main() {
    using namespace NGIN::UI;
    auto result = SDL3::Run(
        {.id = "main", .title = "Hello NGIN.UI", .initialSize = {760, 420}},
        Views::Label("Hello from NGIN.UI"));
    if (!result) {
        std::cerr << result.error().message.CStr() << '\n';
        return 1;
    }
}
```

The desktop host sets up native text, mounts the view, and runs the window.
Use `SDL3::Open` when you need access to the application and its scheduler.

## Build and run

```bash
ngin build --project Hello.UI.nginproj --configuration Debug
ngin run --project Hello.UI.nginproj --configuration Debug
```

The first build may compile SDL3, FreeType, and HarfBuzz. The complete buildable
reference for components and embedding is `Examples/NGIN.UI.Settings`.

You should see a 760 × 420 native window with “Hello from NGIN.UI”. Closing it
should exit `0`.

The Settings example also runs in an engine-owned frame. From the NGIN repository,
use `ngin run --project Examples/NGIN.UI.Settings/NGIN.UI.Settings.nginproj -- --game`.

Continue with [composition and layout](./composition-layout.md) or the
[UI C++ reference](/reference/doxygen/ui/namespaceNGIN_1_1UI.html).
