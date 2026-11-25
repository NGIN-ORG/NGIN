# Build a desktop window with components

A component is a function returning a `NGIN::UI::Views::View`. The SDL desktop
host opens the window, owns native text resources, and drives a retained
`Surface`. Use a component boundary when a part of the screen reads state.

## Product

```xml
<Executable Name="Hello.UI">
  <Uses>
    <Package Name="NGIN.UI" Version="0.4" />
    <Package Name="NGIN.UI.Backend.SDL3" Version="0.4" />
  </Uses>
  <Build><Source Include="src/**/*.cpp" /></Build>
</Executable>
```

## src/main.cpp

```cpp
#include <NGIN/UI/Backend/SDL3/Desktop.hpp>
#include <iostream>
#include <string>

auto Counter() -> NGIN::UI::Views::View {
  using namespace NGIN::UI;
  return Views::Component([](Views::ComponentScope &scope) {
    auto count = scope.Local("count", 0);
    return Views::Column({
        Views::Label(NGIN::Text::String{std::to_string(count.Get())}),
        Views::Button("Increment", [count]() mutable {
          static_cast<void>(count.Set(count.Get() + 1));
        }),
    });
  });
}

int main() {
  auto result = NGIN::UI::SDL3::Run(
      {.id = "main", .title = "Counter", .initialSize = {480, 320}}, Counter());
  if (!result) {
    std::cerr << result.error().message.CStr() << '\n';
    return 1;
  }
}
```

From a workspace exposing these packages, run `ngin run --project Hello.UI.nginproj`.
Use `SDL3::Open` when you need the application before entering the event loop.
Both helpers accept an accessibility backend. The Settings example supplies
the Windows UI Automation provider on Windows.

[Buildable desktop and game example](../../Examples/NGIN.UI.Settings/) ·
[Components and surfaces](ngin-ui-surfaces.md) ·
[Lower-level composition](ngin-ui-application-model.md)
