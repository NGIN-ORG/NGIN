# NGIN.UI Custom Controls

Use ordinary composition functions for controls that can be expressed by
combining existing elements. Derive from `NGIN::UI::ICustomElement` only when a
control needs its own measurement, retained local state, drawing, or routed
input behavior.

The custom-element API is a backend-neutral leaf contract. A custom element
emits the same display-list commands as the built-in controls; it never receives
an SDL object, GPU command buffer, renderer backend, runtime node, or native
window handle.

## Minimal custom element

```cpp
class Meter final : public NGIN::UI::ICustomElement {
public:
  explicit Meter(float value) : m_value(std::clamp(value, 0.0F, 1.0F)) {}

  auto Measure(NGIN::UI::CustomElementContext &,
               NGIN::UI::SizeConstraints constraints)
      -> NGIN::UI::UIResult<NGIN::UI::Size> override {
    return constraints.Constrain({180.0F, 16.0F});
  }

  auto Paint(NGIN::UI::CustomElementContext &,
             NGIN::UI::PaintContext &paint)
      -> NGIN::UI::UIResult<void> override {
    const auto bounds = paint.Bounds();
    paint.FillRounded(bounds, NGIN::UI::CornerRadius::Uniform(
                                  NGIN::UI::Dp{8.0F}),
                      {0.18F, 0.20F, 0.24F, 1.0F});
    paint.FillRounded({0.0F, 0.0F, bounds.width * m_value, bounds.height},
                      NGIN::UI::CornerRadius::Uniform(NGIN::UI::Dp{8.0F}),
                      {0.20F, 0.50F, 0.95F, 1.0F});
    return {};
  }

  auto Semantics(NGIN::UI::CustomElementContext &)
      -> NGIN::UI::UIResult<NGIN::UI::SemanticProperties> override {
    return NGIN::UI::SemanticProperties{
        .role = NGIN::UI::SemanticRole::Slider,
        .label = NGIN::Text::String{"Meter"},
    };
  }

private:
  float m_value;
};
```

Compose the element through the public `Composer` helper and give it a stable
key whenever siblings can reorder:

```cpp
NGIN::UI::NodeProperties properties{};
properties.layout.horizontalAlignment = NGIN::UI::HorizontalAlignment::Start;
properties.layout.verticalAlignment = NGIN::UI::VerticalAlignment::Start;
composer.Custom(std::make_shared<Meter>(0.72F), properties, "download-meter");
```

`Composer::Custom()` creates a leaf. Put it inside a row, column, overlay, or
another composite function when the control also needs child text or controls.

## Lifecycle and ownership

The composer and retained runtime share ownership of the `ICustomElement`
instance. The implementation object can be replaced during recomposition; the
local state store remains attached to the reconciled element identity. A stable
type/key position therefore preserves state even if a fresh implementation
object is supplied each composition.

Callbacks run in this order as needed:

1. `Semantics()` establishes the initial accessible description.
2. `Measure()` receives normalized min/max constraints and returns a desired
   size.
3. `Arrange()` receives the final size after alignment and parent layout.
4. `Paint()` records the current visual output.
5. `PointerEvent()`, `KeyEvent()`, and `TextEvent()` receive routed input.
6. `Unmounted()` runs once before the identity and its state store are removed.

`Measure()`, `Arrange()`, `Paint()`, semantics, and input return `UIResult`.
NGIN.UI reports an error through `NodeProperties::custom.onError` and uses a
safe fallback when a callback fails. Exceptions are caught at every callback
boundary except `Unmounted()`, which is explicitly `noexcept`; cleanup code must
not throw. An exception thrown by an error reporter is swallowed.

Contexts are borrowed and valid only for the duration of the callback. Do not
retain a context, a pointer returned by `State()`, or the `PaintContext`.
Custom-element callbacks and state access run on the application UI thread.
Move work from background threads back through the application dispatcher
before mutating UI state or invalidating a window.

## Retained local state

`CustomElementContext::State<T>()` creates a typed value once for the current
element identity and returns it on later callbacks:

```cpp
auto selected = context.State<std::size_t>("selected-index", 0);
if (!selected) {
  return selected.Error();
}
*selected.Value() = nextIndex;
```

Keys are local to one element. Reusing a key with another C++ type returns
`UIErrorCode::InvalidState` and leaves the original value intact. Values can be
move-only and are destroyed after `Unmounted()` when the identity leaves the
tree. Use `FindState<T>()` when a callback should read state without creating
it.

External application state should continue to use `State<T>` and `Binding<T>`.
The custom store is for implementation details such as drag position,
selection, animation phase, or a small cache that must follow element identity.

## Measurement and arrangement

`Measure()` must honor the supplied `SizeConstraints`; returning
`constraints.Constrain(desired)` is the normal pattern. NGIN.UI constrains the
result again defensively. Layout properties such as minimum/maximum size,
alignment, padding, and flex growth/shrinkage are resolved around the custom
contract just like built-in elements.

`Arrange()` receives local final dimensions. `context.ArrangedSize()` returns
the same dimensions in later callbacks. Input positions are window-local;
convert them with `context.ToLocal(event.position)` before hit-testing custom
geometry.

## Bounded painting

`PaintContext` uses device-independent local coordinates with `(0, 0)` at the
element's arranged top-left. It currently supports solid and rounded fills,
solid and rounded strokes, and existing renderer texture handles.

NGIN.UI inserts a clip equal to the arranged bounds and a translation to the
local origin around every `Paint()` call. Drawing outside `paint.Bounds()` is
therefore clipped. Parent scroll clips, transforms, opacity, and popup ordering
remain in effect, and the framework balances the display-list scopes even when
painting returns an error. DPI conversion happens later in `UIRenderer`.
Solid fills and strokes receive the same one-physical-pixel edge
anti-aliasing as built-in controls; custom controls do not need backend-specific
anti-aliasing code. Text uses FreeType glyph coverage, while images keep their
texture sampling and tint behavior.

Painting must be deterministic and side-effect free. Mutate state from input,
timers, or application state transitions, then invalidate; do not use `Paint()`
as an update loop.

## Input and invalidation

Custom input participates in the existing capture, target, and bubble route.
Use the routed event helpers to mark handling and pointer capture:

```cpp
auto PointerEvent(NGIN::UI::CustomElementContext &context,
                  NGIN::UI::RoutedPointerEvent &event)
    -> NGIN::UI::UIResult<NGIN::UI::InvalidationKind> override {
  if (event.phase == NGIN::UI::EventPhase::Target &&
      event.eventKind ==
          NGIN::UI::RoutedPointerEventKind::ButtonPressed) {
    auto pressed = context.State<bool>("pressed", false);
    if (!pressed) {
      return pressed.Error();
    }
    *pressed.Value() = true;
    event.CapturePointer();
    event.Handle();
    return NGIN::UI::InvalidationKind::Paint |
           NGIN::UI::InvalidationKind::Semantics;
  }
  return NGIN::UI::InvalidationKind::None;
}
```

Return the narrowest invalidation that covers the mutation:

- `Paint` for visual-only state;
- `Semantics` when accessible state/value changes;
- `Arrange` for position-only layout changes;
- `Measure` when desired size changes;
- `Compose` when the declared structure must change.

Set `interaction.focusable` and semantic focus actions for keyboard-focusable
custom controls. Return values do not implicitly mark events handled; call
`event.Handle()` when routing should stop.

## Semantics and examples

`Semantics()` returns the role, label, value, description, states, and actions
owned by the custom element. Explicit semantic values in the authored
`NodeProperties` override the implementation description, which lets an
application localize labels without subclassing.

The public gallery examples in
[`CustomControls.hpp`](../../Examples/NGIN.UI.Gallery/include/NGIN/UIGallery/CustomControls.hpp)
implement a custom status badge, segmented progress ring, and interactive bar
chart. Their source demonstrates composite text, bounded paint, pointer-local
coordinates, retained selection, semantics, and narrow invalidation without
changing a platform or renderer backend.
