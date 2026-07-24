# NGIN.UI Styling

NGIN.UI styling is typed, backend-neutral C++ data. Control code selects theme
tokens and visual states; the display-list renderer lowers the result to
device-independent drawing commands, and the active renderer applies DPI
scaling.

## Theme Tokens

`Theme` groups the standard design tokens:

- `colors`: background and surface levels, foregrounds, accent states, border,
  focus, selection, disabled colors, and errors;
- `typography`: caption, body, and title sizes;
- `spacing`: compact, regular, and spacious gaps;
- `radii`: small, regular, and large corner radii;
- `controls`: compact, regular, and spacious heights plus standard border and
  focus metrics;
- `motion`: standard transition durations.

The default-constructed `Theme` is dark. `MakeLightTheme()` returns the matching
light palette. Applications can copy either value and override individual typed
tokens. A `Theme` can be published through `ThemeResource` in a
`ResourceScope`; controls should receive or resolve that theme during
composition rather than copying colors into control code.

## Visual Properties

Every `NodeProperties` has `visual` properties:

```cpp
NGIN::UI::NodeProperties panel{};
panel.visual = NGIN::UI::MakePanelVisual(theme);
panel.layout.padding = NGIN::UI::Thickness::Uniform(NGIN::UI::Dp{12.0F});
```

`VisualProperties::base` contains optional background, foreground, and border
colors together with per-edge border thickness and per-corner radius.
`foreground` colors text painted by that same node. Composite controls should
pass the matching theme foreground to child `Text` nodes.

Uniform borders follow the configured corner radius. Non-uniform borders are
emitted as four independently sized edge segments. All measurements are in
device-independent units.

`paintsBackground` and `background` remain available for deliberately simple,
unstyled drawing nodes. A `visual.base.background` takes precedence when both
forms are supplied.

## Visual States

The runtime derives hovered, pressed, focused, disabled, and text-field
read-only states. Controls can additionally set selected and invalid states
through `VisualProperties::state`.

State patches are applied in this deterministic order:

1. read-only;
2. selected;
3. invalid;
4. hovered;
5. pressed;
6. focused;
7. disabled.

Later entries win for properties they explicitly override. A patch that does
not specify a property leaves the previously resolved value unchanged.

```cpp
auto visual = NGIN::UI::MakeButtonVisual(theme);
visual.state |= NGIN::UI::VisualStateFlags::Selected;
visual.states.selected.borderColor = theme.colors.focus;
```

The standard helpers are:

- `MakePanelVisual(theme)`;
- `MakeButtonVisual(theme)`;
- `MakeTextFieldVisual(theme)`;
- `MakeSeparatorVisual(theme)`.

## Focus

Button and text-field theme helpers enable an outer focus stroke. It is emitted
whenever the retained node is focused, uses dedicated theme color and metrics,
and is independent of background color so keyboard focus remains visible.
Parent clipping, including a `ScrollView` viewport, still applies.

Applications can disable or replace it through `VisualProperties::focus`.
Custom focus treatments must remain visible in high-contrast themes and must
not be the only indication that a control is disabled or invalid.

## Border and Separator Composition

`Composer::Border()` creates a normal retained container whose children use
overlay arrangement. Padding reserves space inside its visual chrome:

```cpp
ui.Border(
    [&] {
      // Child content.
    },
    panel,
    "settings-card");
```

`Composer::Separator()` creates a decorative horizontal or vertical leaf.
Supply `MakeSeparatorVisual(theme)` and override its preferred cross-axis size
when a rule thicker than one device-independent unit is needed.

## Invalidation

Input-driven hover, press, focus, and enabled-state changes already schedule
paint or semantic invalidation through the input router. Application-owned
selected, invalid, theme, or other visual state changes must invalidate paint;
invalidate measure and arrange as well when a changed token affects padding,
border thickness, or control size.

Changing a theme resource revision does not mutate existing declarations.
Update application state and recompose the affected scope with the new theme.
