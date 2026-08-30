---
title: NGIN.UI quick start
description: Create a standalone SDL3-backed native window with shaped text and retained content.
---

# NGIN.UI quick start

This guide creates a standalone application using the SDL3 backend. The
workspace must provide a package for the `NGIN.UI.Backend` capability.

## Before you start

You need the NGIN CLI, a C++23 compiler, a desktop environment, and a workspace
that discovers `NGIN.UI` plus a compatible backend provider. Create
`Hello.UI/Hello.UI.nginproj` and `Hello.UI/src/main.cpp`.

## Add the product

```xml
<Executable Name="Hello.UI">
  <Uses>
    <Package Name="NGIN.UI" Version="0.4" />
    <Capability Name="NGIN.UI.Backend" Version="1" />
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
#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/UI.hpp>

#include <iostream>
#include <memory>

auto main() -> int {
    using namespace NGIN::UI;

    auto created = CreateApplication(ApplicationCreateInfo{
        .platform = SDL3::CreatePlatformBackend(),
        .renderer = SDL3::CreateRendererBackend(),
        .applicationName = NGIN::Text::String{"Hello NGIN.UI"},
        .enableRendererValidation = true,
    });
    if (!created) {
        std::cerr << created.Error().message.CStr() << '\n';
        return 1;
    }
    auto application = std::move(created).Value();

    auto createdText = NativeTextSystem::Create(application->Renderer());
    if (!createdText) {
        std::cerr << createdText.Error().message.CStr() << '\n';
        return 1;
    }
    auto text = std::move(createdText).Value();

    auto createdWindow = application->CreateWindow(WindowCreateInfo{
        .id = NGIN::Text::String{"main"},
        .title = NGIN::Text::String{"Hello NGIN.UI"},
        .initialSize = PixelSize{760, 420},
    });
    if (!createdWindow) {
        return 1;
    }
    auto* window = createdWindow.Value();

    window->SetContent([&](Composer& composer) {
        NodeProperties heading{};
        heading.text.fontSize = 30.0F;
        heading.text.geometry = text.get();
        heading.semantics.role = SemanticRole::Heading;
        composer.Text(NGIN::Text::String{"Hello from NGIN.UI"},
                      *text, *text, heading, "heading");
    });

    return application->Run().HasValue() ? 0 : 1;
}
```

Keep `application` alive longer than `text`, and both alive longer than the
retained content callback.

## Build and run

```bash
ngin build --project Hello.UI.nginproj --configuration Debug
ngin run --project Hello.UI.nginproj --configuration Debug
```

The first build may compile SDL3, FreeType, and HarfBuzz. The complete buildable
reference is `Examples/NGIN.UI.Gallery`.

You should see a 760 × 420 native window with “Hello from NGIN.UI”. Closing it
should exit `0`.

## If it fails

- Capability resolution failure: inspect the graph and workspace
  `Capabilities/Prefer` selection for `NGIN.UI.Backend`.
- Backend creation failure: print the full `UIError`; check window-system and
  renderer availability, not only package resolution.
- Missing text: keep the native text system alive longer than the retained
  content callback and make sure the font/text backend initialized.
- Headless environment: use the test platform and recording/software renderer
  rather than an SDL native window.

Continue with [composition and layout](./composition-layout.md) or the
[UI C++ reference](../../reference/cpp/ui/index.md).
