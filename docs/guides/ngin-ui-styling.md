# NGIN.UI Styling

NGIN.UI styling is typed, backend-neutral C++ data. Control code selects theme
tokens and visual states; the display-list renderer lowers the result to
device-independent drawing commands, and the active renderer applies DPI
scaling.

The buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) shows both themes, border
variants, focus indicators, disabled states, validation, and resource-scoped
theme overrides.

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

## Accent and control finish

Once the neutral surfaces are set, use `ApplyAccent` to derive hover,
pressed, label, focus and selection colors from one opaque RGB seed. Call
it when a theme changes. The Gallery’s Themes page lets you try this
across the running application.

```cpp
using namespace NGIN::UI;

auto theme = MakeLightTheme();
ApplyAccent(theme, Color{0.09F, 0.40F, 0.29F, 1.0F});
auto save = MakeButtonVisual(theme);
auto cancel = MakeButtonVisual(theme, ButtonAppearance::Neutral);
auto dismiss = MakeButtonVisual(theme, ButtonAppearance::Subtle);
```

Accent buttons use the theme's accent foreground. Neutral and subtle buttons
use its ordinary foreground. The labelled button overload selects its text color
automatically, as in `Views::Button("Cancel", cancel, ButtonAppearance::Neutral)`.
For custom content, pass the matching foreground to child labels.
Setting `theme.gradientControls = false` gives controls a flat finish.

### Drawing a gradient

A background or state patch accepts a solid `Color` or a `LinearGradient`.
The direction is relative to the element’s own bounds. Endpoints can have
different alpha values. The renderer interpolates premultiplied RGBA and
uses the same clipping, corner radii and geometry as a solid fill.

```cpp
NodeProperties preview{};
preview.visual.base.cornerRadius = CornerRadius::Uniform(Dp{8});
preview.visual.base.background = LinearGradient{
    theme.colors.accentHovered, theme.colors.accentPressed,
    GradientDirection::Horizontal};
preview.visual.states.disabled.background = theme.colors.disabledSurface;
```

The other directions are `Vertical`, `DiagonalDown` (top left to bottom
right) and `DiagonalUp` (bottom left to top right). A state patch replaces
the whole brush. With a visual transition configured, the two color stops
animate when the element changes state. To draw the same fill inside a custom
element, call `paint.FillGradient(bounds, radius, gradient)`. The gradient
shares the existing solid-fill geometry and can batch with adjacent untextured
shapes that use the same clip.

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

`Views::Border` creates a normal retained container whose children use
overlay arrangement. Padding reserves space inside its visual chrome:

```cpp
auto card = NGIN::UI::Views::Border({settingsView}, panel).Key("settings-card");
```

`Views::Separator` creates a decorative horizontal or vertical leaf.
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
