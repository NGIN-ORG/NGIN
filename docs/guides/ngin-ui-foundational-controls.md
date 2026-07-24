# NGIN.UI Foundational Controls

`<NGIN/UI/Controls.hpp>` provides the standard controls needed by a small
settings-style application:

- `CheckBox`, with unchecked, checked, and indeterminate values;
- typed `RadioButton` selection;
- `ToggleSwitch`;
- `Slider`;
- determinate and indeterminate `ProgressBar`;
- semantic `Label` association;
- delayed `ToolTipController`;
- visual, draggable, keyboard-operable `ScrollView` scrollbars.

They are also available through the aggregate `<NGIN/UI/UI.hpp>` header. All
controls are backend-neutral and use the same public custom-element contract as
application-authored controls.

## Bind Controls to Model State

Keep `State<T>` in the view model and compose a short-lived `Binding<T>`:

```cpp
using namespace NGIN::UI;

State<CheckState> automaticUpdates{CheckState::Indeterminate};
State<bool> darkMode{true};
State<F32> volume{0.65F};

window.SetContent([&](Composer &composer) {
  CheckBox(composer, Bind(automaticUpdates), {}, {}, "automatic-updates");
  ToggleSwitch(composer, Bind(darkMode), {}, {}, "dark-mode");
  Slider(composer, Bind(volume),
         SliderRange{.minimum = 0.0F, .maximum = 1.0F, .step = 0.05F}, {},
         {}, "volume");
});
```

The model state and anything captured by a binding must outlive composed
elements. Pointer and keyboard changes use `Binding<T>::Set`; validation errors
are delivered through `ControlPresentation::onError`.

## Typed Radio Groups

`BindRadio` adapts any equality-comparable typed binding into one radio choice:

```cpp
enum class Density { Compact, Comfortable, Spacious };
State<Density> density{Density::Comfortable};

RadioButton(composer, BindRadio(Bind(density), Density::Compact), {}, {},
            "compact");
RadioButton(composer, BindRadio(Bind(density), Density::Comfortable), {}, {},
            "comfortable");
RadioButton(composer, BindRadio(Bind(density), Density::Spacious), {}, {},
            "spacious");
```

The group value remains the application's type; there is no integer index or
string conversion.

## Theme, Validation, Disabled, and Focus States

Pass a `Theme` through `ControlPresentation`. Mark validation independently
from enabled state:

```cpp
ControlPresentation invalid{
    .theme = theme,
    .invalid = true,
    .onError = [](const UIError &error) { Report(error); },
};

NodeProperties properties{};
properties.interaction.enabled = canEdit;
properties.semantics.label = NGIN::Text::String{"Volume"};
Slider(composer, Bind(volume), {}, invalid, properties, "volume");
```

Controls resolve colors from the supplied theme. Disabled presentation takes
precedence over interaction feedback. Focusable controls participate in Tab
navigation and draw the theme focus color. Check boxes, radio buttons, and
switches activate with Space or Enter. Sliders support arrows plus Home and
End, and capture the pointer while dragging.

`ProgressBar` is read-only:

```cpp
ProgressBar(composer,
            ProgressValue{.value = progress,
                          .minimum = 0.0F,
                          .maximum = 100.0F},
            ControlPresentation{.theme = theme}, {}, "download-progress");

ProgressBar(composer, ProgressValue{.indeterminate = true},
            ControlPresentation{.theme = theme}, {}, "working");
```

## Associate Labels

Semantic identifiers explicitly connect visible labels to controls:

```cpp
NodeProperties labelProperties{};
labelProperties.text.color = theme.colors.foreground;
Label(composer, NGIN::Text::String{"Volume"}, textLayout, glyphAtlas,
      "volume-label", "volume-control", labelProperties, "label");

NodeProperties sliderProperties{};
sliderProperties.semantics.identifier =
    NGIN::Text::String{"volume-control"};
sliderProperties.semantics.labelledBy =
    NGIN::Text::String{"volume-label"};
Slider(composer, Bind(volume), {}, {}, sliderProperties, "slider");
```

The semantic tree exposes `identifier`, `labelFor`, and `labelledBy` on each
`SemanticNode`; platform accessibility bridges can resolve the relationship
without inspecting layout or adjacent text.

## Delayed Tooltips

A controller owns hover delay and open state. It must outlive every composition
that attaches or renders it:

```cpp
ToolTipController help{
    window, NGIN::Text::String{"Changes the application theme."},
    std::chrono::milliseconds{500}};

window.SetContent([&](Composer &composer) {
  NodeProperties target{};
  help.Attach(target);
  composer.Button([] {}, target, "theme-button");

  help.Compose(composer, [&] {
    composer.Leaf(ElementType::Border, "tooltip-surface");
  });
});
```

The controller uses `Window::Schedule`, cancels pending work when the pointer
leaves, and emits a non-modal popup. Opening it does not move or clear keyboard
focus. `Attach` also copies the help text into the target's semantic
description, so the information is not hover-only.

## Scrollbars

Scrollbars appear automatically when enabled content exceeds the viewport:

```cpp
NodeProperties scroll{};
scroll.layout.preferredSize = Size{420.0F, 260.0F};
scroll.interaction.focusable = true;
scroll.scroll.vertical = true;
scroll.scroll.horizontal = false;
scroll.scroll.showScrollbars = true;
scroll.scroll.scrollbarTrack = theme.colors.sunkenSurface;
scroll.scroll.scrollbarThumb = theme.colors.border;
scroll.scroll.scrollbarThumbHovered = theme.colors.focus;

composer.ScrollView([&] { ComposeLongContent(composer); }, scroll,
                    "settings-scroll");
```

The mouse wheel chains to an ancestor scroll view when the nearest view cannot
move further. A scrollbar thumb captures the pointer while dragging; clicking
its track jumps toward that location. A focused scroll view accepts arrow,
Home, and End keys. Set `showScrollbars` to `false` only when another visible
scroll affordance is supplied.

## Semantics

| Control | Role | Important state/value |
| --- | --- | --- |
| `CheckBox` | `CheckBox` | checked, unchecked, or indeterminate |
| `RadioButton` | `RadioButton` | selected/checked |
| `ToggleSwitch` | `Switch` | on/off and checked |
| `Slider` | `Slider` | current numeric value and range actions |
| `ProgressBar` | `ProgressBar` | numeric or indeterminate |

Disabled and focused flags come from the retained interaction state. Slider
semantics expose SetValue, Increment, and Decrement actions.

See the buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) Inputs page for complete
state matrices and the deterministic tests in
[`ControlsTests.cpp`](../../Packages/NGIN.UI/tests/ControlsTests.cpp).
