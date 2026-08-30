# NGIN.UI: First Window in Five Minutes

This tutorial creates a standalone native NGIN.UI application with SDL3,
SDL_GPU, FreeType, HarfBuzz, and the bundled OFL Noto Sans font.

The complete buildable reference is
[`NGIN.UI.Gallery`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/). Its
[`main.cpp`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/src/main.cpp) uses the same startup
sequence shown here.

## 1. Add the product manifest

Create `Hello.UI.nginproj`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Executable Name="Hello.UI">
  <Uses>
    <Package Name="NGIN.UI" Version="0.4" />
    <Capability Name="NGIN.UI.Backend" Version="1" />
  </Uses>
  <Build><Source Include="src/**/*.cpp" /></Build>
</Executable>
```

The workspace preference selects `NGIN.UI.Backend.SDL3`, which brings in SDL3
transitively. Fonts and legal notices are
required package contributions, so applications never need magic features to
receive them. The project still declares `NGIN.UI` directly because its source
uses that public API.

## 2. Create the application

Create `src/main.cpp`:

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
    std::cerr << createdWindow.Error().message.CStr() << '\n';
    return 1;
  }
  auto *window = createdWindow.Value();

  window->SetContent([&](Composer &composer) {
    NodeProperties root{};
    root.layout.padding = Thickness::Uniform(Dp{32.0F});
    root.layout.gap = 12.0F;
    root.visual.base.background = Color{0.05F, 0.06F, 0.08F, 1.0F};

    composer.Element(
        ElementType::Column, root,
        [&] {
          NodeProperties heading{};
          heading.layout.horizontalAlignment = HorizontalAlignment::Start;
          heading.text.fontSize = 30.0F;
          heading.text.color = Color{0.92F, 0.95F, 1.0F, 1.0F};
          heading.text.geometry = text.get();
          heading.semantics.role = SemanticRole::Heading;
          composer.Text(NGIN::Text::String{"Hello from NGIN.UI"}, *text, *text,
                        heading, "heading");

          NodeProperties body = heading;
          body.text.fontSize = 16.0F;
          body.text.color = Color{0.65F, 0.7F, 0.78F, 1.0F};
          body.text.wrapping = TextWrapping::Wrap;
          body.semantics.role = SemanticRole::Text;
          composer.Text(
              NGIN::Text::String{
                  "One retained view, shaped by HarfBuzz and drawn by SDL_GPU."},
              *text, *text, body, "body");
        },
        "root");
  });

  auto run = application->Run();
  if (!run) {
    std::cerr << run.Error().message.CStr() << '\n';
    return 1;
  }
  return 0;
}
```

Keep `application` alive longer than `text`, and keep both alive longer than
the window content callback. Captures in `SetContent()` are retained, not
one-shot.

## 3. Build and run

From the workspace:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project path/to/Hello.UI.nginproj `
  --configuration Debug `
  --output build/manual/Hello.UI

build/manual/Hello.UI/bin/Hello.UI.exe
```

The first build may restore and compile SDL3, FreeType, and HarfBuzz. Later
builds reuse the staged package graph.

## 4. Add interaction

Application state belongs outside the composition callback. Use `State<T>` or
a model object, provide an invalidation callback, mutate through `Binding<T>`,
and keep keys stable:

```cpp
State<NGIN::Text::String> name{
    NGIN::Text::String{"Ada"},
    [window](InvalidationKind kind) { window->Invalidate(kind); }};

// Inside SetContent():
composer.TextField(Bind(name), *text, fieldProperties, "name");
```

Continue with the
[composition, layout, and state guide](ngin-ui-application-model.md), then use
the gallery's `Inputs`, `Collections`, `Text Area`, and `Images` pages as a
catalogue of buildable public-API examples.

## Common first-run failures

- An unknown package means the workspace does not expose the NGIN.UI package
  providers.
- A native-text startup error usually means the staged font is missing or the
  configured `fontPath` is wrong.
- An SDL or shader startup error belongs to the selected backend, not the view.
- A black/empty window commonly means no content callback was installed or all
  children measured to zero.

See [troubleshooting](ngin-ui-troubleshooting.md) for diagnostic commands and
the structured error fields to log.
