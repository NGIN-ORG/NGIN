# NGIN.UI Collections and Navigation

`<NGIN/UI/CollectionViews.hpp>` provides the model and composition foundations for
selectable lists, combo boxes, tabs, menu buttons, context menus, and future
incremental collections. Its view factories live in `NGIN::UI::Views`. The header is also included by `<NGIN/UI/UI.hpp>`.

## Typed Selection

Choose a model that states the collection's policy in its type:

```cpp
using namespace NGIN::UI;

NoSelectionModel<ItemId> passive;
SingleSelectionModel<ItemId> current{ItemId{42}, scheduleInvalidation};
MultipleSelectionModel<ItemId> checked{{ItemId{4}, ItemId{8}},
                                       scheduleInvalidation};
```

The models expose `IsSelected`, `Select`, and `Clear`; the multiple model also
has `Deselect` and `Toggle`. `SingleSelectionModel::ValueBinding` and
`MultipleSelectionModel::ValueBinding` expose their full values when another
control needs a binding.

`BindListItem` adapts a model—or an ordinary typed `Binding<T>`—to
`ItemSelection`. The adapter avoids integer indices and keeps selection stable
when the view is sorted or filtered.

## Compose a List

Give every data item a stable key. Do not use its current position:

```cpp
NodeProperties list{};
list.layout.preferredSize = Size{420.0F, 280.0F};
list.interaction.focusable = true;
list.scroll.vertical = true;
list.semantics.role = SemanticRole::List;
list.semantics.label = NGIN::Text::String{"Project files"};

auto filesView = Views::Component([&, list] {
  std::vector<Views::View> rows;
  for (const auto &item : VisibleItems()) {
    NodeProperties row{};
    row.layout.preferredSize.height = 40.0F;
    row.semantics.label = item.name;
    rows.push_back(Views::SelectableListItem(BindListItem(selection, item.id),
                                            Views::Label(item.name), row)
                       .Key(item.stableKey.View()));
  }
  return Views::Box(ElementType::ListView, {Views::Column(std::move(rows))}, list);
}).Key("files");
```

`ListView` is a semantic list and `SelectableListItem` emits semantic
`ListItem`, selected state, and activation. A focused list supports:

- Up/Down and Left/Right selection movement;
- Home and End;
- a 750 ms case-insensitive type-ahead prefix;
- automatic scrolling to keep the selected item visible;
- pointer selection while focus remains on the list.

Keyed reconciliation preserves each retained node and its local state through
insert, remove, reorder, and filtering. Removing an item really unmounts it;
filtering it out does the same unless the application deliberately keeps a
collapsed declaration.

## Combo Boxes

Keep a `PopupController` in the view model and give the anchor a semantic
identifier that is unique within the window:

```cpp
PopupController densityPopup{scheduleInvalidation};
auto combo = Views::Component([&] {
  std::vector<Views::View> options;
  for (auto value : densityOptions) {
    auto selection = BindListItem(Bind(density), value);
    auto select = std::move(selection.select);
    selection.select = [&, select = std::move(select)] {
      auto result = select();
      if (result) densityPopup.Close();
      return result;
    };
    options.push_back(Views::SelectableListItem(std::move(selection), OptionView(value))
                          .Key(KeyFor(value)));
  }
  return Views::ComboBox(densityPopup, "density-combo", SummaryView(density.Get()),
                         Views::Column(std::move(options)), buttonProperties, popupProperties);
});
```

The control button exposes `ComboBox` and expanded semantics. Its popup is
positioned from the anchor's current arranged bounds, flips when needed, and
contains a focused `ListView`, so the same arrow and type-ahead behavior is
used. Call `PopupController::Close` after committing an option. Escape and an
outside primary click dismiss the popup and restore focus. Enter, Space, Up, or
Down opens a focused combo box.

## Retained Tabs

`Tabs` takes a typed binding and stable `TabDefinition<T>` entries:

```cpp
const std::array definitions{
    TabDefinition<Page>{Page::General, NGIN::Text::String{"general"},
                        NGIN::Text::String{"General"}},
    TabDefinition<Page>{Page::Advanced, NGIN::Text::String{"advanced"},
                        NGIN::Text::String{"Advanced"}},
};

auto tabs = Views::Tabs<Page>(
    Bind(page), definitions,
    [](const auto &tab, bool) { return Views::Label(tab.label); },
    [&](const auto &tab, bool) { return PageContent(tab.value); },
    presentation).Key("settings-tabs");
```

Every panel remains declared with its stable key. Inactive panels use
`ElementVisibility::Collapsed`: they take no layout space, do not paint,
receive input, or enter the semantic tree, but their runtime nodes and custom
local state remain mounted. Left/Right, Up/Down, Home, and End move focus and
selection among tab headers.

`ElementVisibility::Hidden` is the alternative when content should keep its
layout space while being absent from painting, hit testing, focus, popups, and
semantics.

## Menus

`MenuButton` uses the same anchored popup controller:

```cpp
auto duplicateItem = Views::MenuItem(Views::Label("Duplicate"), [&] {
  DuplicateSelection();
  actionsMenu.Close();
}).Key("duplicate");
auto menu = Views::MenuButton(actionsMenu, "actions-button", Views::Label("Actions"),
                              Views::Column({duplicateItem}));
```

Menu items are focusable, activate with pointer, Enter, or Space, and expose
`MenuItem` semantics. Up/Down and Home/End move through a menu; Escape and
outside-pointer dismissal are supplied by the popup.

For a context menu, attach the secondary-button handler to a target and compose
the popup beside it:

```cpp
AttachContextMenu(targetProperties, contextMenu);
auto target = Views::Border({TargetView()}, targetProperties).Key("target");
auto panel = Views::Overlay(
                        {target, Views::ContextMenu(contextMenu, ContextItems(contextMenu))});
```

The controller records the pointer position as the popup anchor. It must
outlive the composed handlers.

## Virtualized Lists

Use an ordinary `ListView` for small in-memory collections. Use
`Views::VirtualizedList` when the logical collection is large or loaded in ranges.
The two paths are separate, so adding virtualization does not change an
existing list.

Version 0.2 uses an explicit fixed-size contract. Every row in one virtualized
list has the same `itemExtent` and `itemGap`. Variable-height rows are not
supported yet.

Keep the controller in the view model so it survives repeated composition:

```cpp
FixedVirtualizedListController filesController{
    FixedVirtualizationOptions{
        .itemExtent = 40.0F,
        .itemGap = 2.0F,
        .overscanItems = 3,
        .initialViewportExtent = 320.0F,
    },
    scheduleInvalidation};
```

The source and controller must both outlive the composed list. If they are
members of the same view model, declare the source before the controller so
the controller is destroyed first and can cancel its last range safely.

The source implements `IVirtualizedDataSource<T>`:

- `Count()` reports the full logical item count;
- `Revision()` changes after insert, remove, reorder, filter, or a requested
  range becoming available;
- `KeyAt()`, `LabelAt()`, and `IndexOfKey()` work for every logical item,
  including items that are not loaded or on screen;
- `RequestRange()` starts loading the requested viewport plus overscan;
- `CancelRange()` stops obsolete work when the viewport moves;
- `ItemAt()` returns the loaded value for a requested index.

Stable keys are required. They preserve selection, the top visible item,
retained row identity, and accessibility identity when source indices change.
Do not use the current source index as the key if items can move.

Compose only the range selected by the controller:

```cpp
VirtualizedListPresentation view{};
view.list.layout.preferredSize = Size{640.0F, 320.0F};
State<NGIN::Text::String> selectedKey;
view.selectedIndex = [&] { return source.IndexOfKey(selectedKey.Get()); };
view.isSelected = [&](UIntSize index) {
  return source.IndexOfKey(selectedKey.Get()) == index;
};
view.activate = [&](UIntSize index) -> UIResult<void> {
  auto key = source.KeyAt(index);
  if (!key) {
    return std::unexpected(std::move(key).error());
  }
  static_cast<void>(selectedKey.Set(std::move(key).value()));
  return {};
};
view.onError = ReportUIError;

auto files = Views::VirtualizedList<Item>(
    filesController, source,
    [](const Item &item, UIntSize) { return Views::Label(item.name); }, view).Key("files");
```

The controller requests the visible range plus `overscanItems`, gives the list
its full logical scroll height, and composes, measures, paints, and exposes
semantics only for realized rows. `Diagnostics()` reports the logical count,
realized range and mappings, viewport, total extent, source revision, and
request/cancellation counts. The same data is copied into
`LayoutPassStats::virtualizedLists` after layout.

A focused virtualized list supports arrows, Home, End, and the same 750 ms
case-insensitive type-ahead behavior as an ordinary list. Navigation can select
and reveal an item that is not currently realized. Selection remains stored as
a stable source key, focus stays on the list owner, and the semantic tree
publishes a stable virtual item until the selected row is realized. Native
accessibility providers can then use Select, Scroll Into View, and Realize.

For asynchronous loading, keep the requested range alive until it completes,
store the new revision in a `State<UInt64>` and return its value from
`Revision()`. The list observes that read and rebuilds when the revision changes. When the new revision is
observed, the controller anchors the current top key and its within-row offset.
It also cancels and repeats the current range request against the new revision,
so `RequestRange()` should be idempotent. The same anchoring path covers
insertion, removal, reordering, and filtering.

`VectorDataSource<T>` remains the small adapter for an existing span. It does
not provide stable-key virtualization; implement `IVirtualizedDataSource<T>`
when the virtual adapter is needed.

See the buildable
[`NGIN.UI.Gallery`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/) Collections page and
[`CollectionTests.cpp`](https://github.com/NGIN-ORG/NGIN/tree/main/Packages/NGIN.UI/tests/CollectionTests.cpp) for
complete public-API examples.
