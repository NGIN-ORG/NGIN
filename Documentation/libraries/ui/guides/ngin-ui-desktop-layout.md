# Desktop Layout

NGIN.UI provides three desktop layout primitives in addition to rows, columns,
padding, borders, overlays, and scrolling:

- `Grid` builds forms and dashboards from rows and columns.
- `WrapPanel` moves items onto new lines when space becomes narrow.
- `Canvas` places diagram or overlay content at explicit offsets.

All sizes are logical units. The layout engine keeps them independent of the
window DPI scale.

## Grid

Define tracks on the Grid and placement on each direct child:

```cpp
NodeProperties form{};
form.grid.columns = {
    GridTrack::Auto(120.0F, 180.0F),
    GridTrack::Weighted(1.0F),
};
form.grid.rows = {GridTrack::Auto(), GridTrack::Auto()};
form.grid.columnGap = 12.0F;
form.grid.rowGap = 8.0F;

composer.Grid(
    [&] {
      NodeProperties label{};
      label.gridPlacement = GridPlacement{.row = 0, .column = 0};
      composer.Element(ElementType::Column, label, [&] {
        // Compose the label.
      }, "name-label");

      NodeProperties field{};
      field.gridPlacement = GridPlacement{
          .row = 0, .column = 1, .rowSpan = 1, .columnSpan = 1};
      composer.Border([&] {
        // Compose the field.
      }, field, "name-field");
    },
    form, "settings-form");
```

Use `GridTrack::Fixed(size)` for a known size, `GridTrack::Auto()` for content
size, and `GridTrack::Weighted(weight)` to share the remaining space. Automatic
and weighted tracks accept minimum and maximum bounds. A Grid with no authored
rows or columns uses one weighted row and column.

Direct children default to row 0, column 0, with a span of one. A span that
extends past the final track is safely limited to the available tracks.

## WrapPanel

`WrapPanel` measures direct children and starts a new line when the next item
would exceed the available main-axis space:

```cpp
NodeProperties toolbar{};
toolbar.wrapPanel.orientation = WrapOrientation::Horizontal;
toolbar.wrapPanel.itemGap = 8.0F;
toolbar.wrapPanel.lineGap = 8.0F;
toolbar.wrapPanel.lineAlignment = WrapLineAlignment::Start;

composer.WrapPanel([&] {
  ComposeAction("New");
  ComposeAction("Open");
  ComposeAction("Save");
  ComposeAction("Export");
}, toolbar, "toolbar");
```

Orientation can be horizontal or vertical. Each line can align at the start,
center, end, or distribute its remaining space between items. Collapsed items
do not reserve space or create gaps.

## Canvas

Canvas is intentionally small and bounded. Use it for diagrams and custom
surfaces, not ordinary forms:

```cpp
NodeProperties canvas{};
canvas.layout.preferredSize = Size{640.0F, 240.0F};
canvas.canvas.clipToBounds = true;

composer.Canvas([&] {
  NodeProperties node{};
  node.canvasPlacement.offset = Point{40.0F, 24.0F};
  node.canvasPlacement.contributesToDesiredSize = false;
  composer.Border([&] {
    // Compose one diagram node.
  }, node, "source-node");
}, canvas, "diagram");
```

Offsets are relative to the Canvas content area. Children can contribute their
offset and measured size to the Canvas desired size, or opt out when the Canvas
has an explicit size. Canvas clips overflowing children by default; set
`clipToBounds` to `false` only when the parent surface deliberately owns the
overflow.

## Responsive windows

Prefer weighted Grid tracks and wrapping toolbars over fixed page widths. Give
important content a realistic minimum track size, then test the actual minimum
window size. A scroll view can contain Grid and WrapPanel content; Grid resolves
an intrinsic size when an axis is unbounded and shares available space when it
is finite.

The latest `Window::LastLayoutStats()` and `WindowDiagnostics::layout` expose
each Grid's resolved row and column sizes and every WrapPanel line's item count
and extents. The Gallery Diagnostics page displays the same information.
