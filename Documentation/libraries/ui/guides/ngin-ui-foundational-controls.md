# NGIN.UI Foundational Controls

`<NGIN/UI/ViewControls.hpp>` provides the standard controls needed by a small
settings-style application:

- `CheckBox`, with unchecked, checked, and indeterminate values;
- typed `RadioButton` selection;
- `ToggleSwitch`;
- `Slider`;
- determinate and indeterminate `ProgressBar`;
- semantic label association;
- delayed tooltips with `Views::ToolTip` and `ToolTipController`;
- visual, draggable, keyboard-operable `ScrollView` scrollbars.

They are also available through the aggregate `<NGIN/UI/UI.hpp>` header. Each
factory returns a backend-neutral `Views::View` that can be keyed, nested, and mounted on a surface.

## Bind Controls to Model State

Keep `State<T>` in the view model and compose a short-lived `Binding<T>`:

```cpp
using namespace NGIN::UI;

State<CheckState> automaticUpdates{CheckState::Indeterminate};
State<bool> darkMode{true};
State<F32> volume{0.65F};

auto controls = Views::Column({
    Views::CheckBox(Bind(automaticUpdates)).Key("automatic-updates"),
    Views::ToggleSwitch(Bind(darkMode)).Key("dark-mode"),
    Views::Slider("Volume", Bind(volume),
                  {.minimum = 0.0F, .maximum = 1.0F, .step = 0.05F}).Key("volume"),
});
window.SetContent(controls, Views::ViewEnvironment{text});
```

The model state and anything captured by a binding must outlive composed
elements. Pointer and keyboard changes use `Binding<T>::Set`; validation errors
are delivered through `ControlPresentation::onError`.

## Typed Radio Groups

`BindRadio` adapts any equality-comparable typed binding into one radio choice:

```cpp
enum class Density { Compact, Comfortable, Spacious };
State<Density> density{Density::Comfortable};

auto choice = [&](Density value, const char *label) {
  NodeProperties properties{};
  properties.semantics.label = NGIN::Text::String{label};
  return Views::RadioButton(BindRadio(Bind(density), value), {}, properties).Key(label);
};
auto choices = Views::Column({choice(Density::Compact, "Compact"),
                              choice(Density::Comfortable, "Comfortable"),
                              choice(Density::Spacious, "Spacious")});
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
auto slider = Views::Slider(Bind(volume), {}, invalid, properties).Key("volume");
```

Controls resolve colors from the supplied theme. Disabled presentation takes
precedence over interaction feedback. Focusable controls participate in Tab
navigation and draw the theme focus color. Check boxes, radio buttons, and
switches activate with Space or Enter. Sliders support arrows plus Home and
End, and capture the pointer while dragging.

`ProgressBar` is read-only:

```cpp
auto progressBars = Views::Component([&] {
  return Views::Column({
      Views::ProgressBar({.value = progress.Get(), .minimum = 0.0F, .maximum = 100.0F},
                         {.theme = theme}).Key("download-progress"),
      Views::ProgressBar({.indeterminate = true}, {.theme = theme}).Key("working"),
  });
});
```

## Associate Labels

Semantic identifiers explicitly connect visible labels to controls:

```cpp
NodeProperties label{};
label.text.color = theme.colors.foreground;
label.semantics.identifier = NGIN::Text::String{"volume-label"};
label.semantics.labelFor = NGIN::Text::String{"volume-control"};

NodeProperties control{};
control.semantics.identifier = NGIN::Text::String{"volume-control"};
control.semantics.labelledBy = label.semantics.identifier;

auto volumeRow = Views::Row({Views::Label("Volume", label),
                             Views::Slider(Bind(volume), {}, {}, control)});
```

The semantic tree exposes `identifier`, `labelFor`, and `labelledBy` on each
`SemanticNode`; platform accessibility bridges can resolve the relationship
without inspecting layout or adjacent text.

## Delayed Tooltips

A controller owns hover delay and open state. It must outlive every composition
that attaches or renders it:

```cpp
ToolTipController help{window, NGIN::Text::String{"Changes the application theme."}};
NodeProperties target{};
target.semantics.label = NGIN::Text::String{"Dark mode"};
help.Attach(target);
auto themeSwitch = Views::Overlay({
    Views::ToggleSwitch(Bind(darkMode), {}, target),
    Views::ToolTip(help, Views::Label("Changes the application theme.")),
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

auto viewport = Views::ScrollView({settingsPanel}, scroll)
                    .Key("settings-scroll");
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
[`NGIN.UI.Gallery`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/) Inputs page for complete
state matrices and the deterministic tests in
[`ControlsTests.cpp`](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.UI/tests/ControlsTests.cpp).

## Editing reflected settings

The Gallery’s **Property Editor** uses a checkbox and a slider to edit a reflected
playback object. It enumerates properties at composition time, reads their values,
and checks `Property::IsWritable()` before creating an editable binding. The
output level comes from a getter on the same object and updates after either edit.
The Gallery products declare their Reflection dependency. See the
[page implementation](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/src/GalleryPropertyEditor.cpp).
