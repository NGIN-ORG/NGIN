# NGIN.UI Composition, Layout, and State

This guide explains the retained application model: how declarations become
runtime nodes, how constraints flow, where state lives, and when to invalidate.

The buildable
[`NGIN.UI.Gallery`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/) demonstrates every rule in
this guide. Its
[`GalleryViewModel`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/include/NGIN/UIGallery/Gallery.hpp)
owns state while
[`MainView()`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/src/Gallery.cpp) returns a component tree that either host mounts on a surface.

## Composition is a declaration pass

Mount a view on a window with its text and theme environment. Each mounted
component tracks the observable values it reads. Its next build emits a new
declaration for reconciliation with the retained tree.

```cpp
window.SetContent(
    Views::Column({Views::Label("Profile").Key("title"),
                   Views::TextField("Name", Bind(model.name)).Key("name")}),
    Views::ViewEnvironment{text});
```

To build a reusable control, write a function that returns a view. A container
takes its children as values.

```cpp
auto Toolbar(CommandBinding save, CommandBinding close) -> Views::View {
  return Views::Row({
      Views::Button(save, Views::Label("Save")).Key("save"),
      Views::Button(close, Views::Label("Close")).Key("close"),
  }).Key("toolbar");
}
```

## Keys and reconciliation

Reconciliation matches each new declaration to the previous children:

1. keyed declarations match a previous sibling with the same key and type;
2. unkeyed declarations match by compatible sibling position;
3. unmatched declarations create runtime nodes;
4. unmatched runtime nodes unmount and are destroyed.

Keys are local to a parent. Use stable product identity, not display position:

```cpp
std::vector<Views::View> rows;
for (const auto &task : tasks) {
  rows.push_back(TaskRow(task).Key(std::to_string(task.id)));
}
auto list = Views::Column(std::move(rows));
```

Stable keys preserve focus, editing sessions, scroll offsets, custom local
state, and semantic ownership through insert/remove/reorder operations.
Index-based keys deliberately attach state to positions and are almost never
correct for mutable collections.

Changing an element type with the same key replaces the node. Moving a key to a
different parent also creates a new identity.

## Retained state ownership

Keep shared application state in a model and component-local state in
`ComponentScope::Local`.

```cpp
struct Model {
  State<NGIN::Text::String> name{NGIN::Text::String{"Ada"}};
  State<NGIN::UInt16> port{8080};
};
```

`Bind(state)` supplies read/write access. Reading the binding in a mounted
component subscribes that component to later changes. Keep the model alive
while its controls are mounted. An expired state binding rejects writes.

Use `WithValidation()` for transactional rejection:

```cpp
auto binding = Bind(model.port).WithValidation(
    [](const NGIN::UInt16 value) -> UIResult<void> {
      if (value == 0) {
        return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                                           "Port zero is reserved"));
      }
      return {};
    });
```

Controls commit through the binding. On error, the source value and retained
editing buffer remain unchanged. Route errors to
`ControlPresentation::onError`, `TextFieldProperties::onError`, or the
corresponding property callback.

Prefer one model owner per logical window. Share immutable services and
resources; make cross-window mutable state explicit.

## Invalidation

Invalidation declares the cheapest work that may produce a correct frame:

| Kind | Recompose | Layout | Paint |
|---|---:|---:|---:|
| `Compose` | yes | yes | yes |
| `Measure` | no | yes | yes |
| `Arrange` | no | yes | yes |
| `Paint` | no | no | yes |
| `Semantics` | no | no | no |
| `All` | yes | yes | yes |

State that changes declarations, text, child order, bindings, semantics, or
properties needs `Compose` (or `All`). A retained custom element returns
`Measure`, `Arrange`, or `Paint` invalidation from its input and lifecycle
callbacks.

Invalidations coalesce until the next application pump. Do not force immediate
layout after every mutation.

## Constraint layout

Measurement flows downward as `SizeConstraints`; desired sizes flow upward.
Arrangement then assigns final `Rect` values downward.

- `minimum` and `maximum` constraints are always authoritative.
- `preferredSize` is a request, not an override.
- node-level minimum and maximum sizes further narrow parent constraints.
- padding is inside the arranged bounds and outside child content.
- `gap` exists only between non-collapsed flow children.
- `Collapsed` contributes no size; `Hidden` retains size but does not paint,
  hit-test, focus, or emit semantics.

Rows measure children with an unbounded horizontal axis. Columns use an
unbounded vertical axis. `flexGrow` distributes positive main-axis space;
`flexShrink` distributes a deficit using desired size as weight.

`horizontalAlignment` and `verticalAlignment` position a child on its parent's
cross axis. `Stretch` consumes the available dimension while still respecting
minimum and maximum sizes.

## Units and DPI

Layout geometry uses device-independent floating-point units. `Dp` makes that
intent explicit:

```cpp
properties.layout.padding = Thickness::Uniform(Dp{12.0F});
properties.visual.base.cornerRadius = CornerRadius::Uniform(Dp{6.0F});
```

Platform window and texture extents use integer pixels (`PixelSize`,
`PixelRect`). The window scale factor converts layout and input coordinates to
pixels. Backends report scale changes; NGIN.UI relayouts and rerasterizes
scale-sensitive glyphs.

Do not pre-scale layout properties. Custom painting also uses local
device-independent coordinates; use `CustomElementContext::ScaleFactor()` only
for pixel-sensitive resource decisions.

## Scrolling

`ScrollView`, `ListView`, and `TextArea` retain logical offsets. A scroll view
measures an enabled axis as unbounded, clips its children to the viewport, and
clamps offsets after arrange.

```cpp
NodeProperties scroll{};
scroll.layout.preferredSize = {520.0F, 240.0F};
scroll.scroll.vertical = true;
scroll.scroll.horizontal = false;
scroll.scroll.wheelStep = 36.0F;
auto viewport = Views::ScrollView({longContent}, scroll).Key("viewport");
```

Wheel input chains to a scrollable ancestor when the inner view cannot move.
Visible scrollbars support pointer dragging and keyboard movement. Focus the
scroll view to use arrow, Home, and End keys.

Avoid putting an unbounded scroll view on the same axis as an unconstrained
parent: there is then no finite viewport to scroll.

## Resources and services

Pass durable services—text layout, glyph atlas, image resolver, theme, and
application model—into the composition owner. Element properties contain
non-owning pointers to services. Logical image resources use `shared_ptr`, but
their `ImageTextureCache` remains externally owned.

The normal lifetime order is:

1. platform and renderer;
2. `Application`;
3. text system and renderer-bound caches;
4. models;
5. windows and content callbacks;
6. destroy windows/models/caches/text before the application.

## Testing the model

Use `TestPlatformBackend` and `RecordingRenderBackend` to create a real
`Application`, pump deterministic frames, and inspect:

- `Window::Tree()` and `LastReconcileStats()`;
- `Window::Semantics()`;
- `Window::Diagnostics()`;
- recorded texture updates and render packets.

The buildable
[`NGIN.UI.Gallery.Tests`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery.Tests/) is the
canonical end-to-end example. Focused unit examples live in
[`Packages/NGIN.UI/tests`](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.UI/tests/).
