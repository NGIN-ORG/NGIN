# NGIN.UI Collections and Navigation

`<NGIN/UI/Collections.hpp>` provides the model and composition foundations for
selectable lists, combo boxes, tabs, menu buttons, context menus, and future
incremental collections. The header is also included by `<NGIN/UI/UI.hpp>`.

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
list.semantics.label = NGIN::Text::String{"Project files"};

composer.ListView(
    [&] {
      composer.Column([&] {
        for (const auto &item : VisibleItems()) {
          NodeProperties row{};
          row.layout.preferredSize.height = 40.0F;
          row.semantics.label = item.name;
          SelectableListItem(
              composer, BindListItem(selection, item.id),
              [&] { ComposeItemContents(composer, item); }, row, {},
              item.stableKey);
        }
      });
    },
    list, "files");
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

ComboBox(
    composer, densityPopup, "density-combo",
    [&] { ComposeSummary(composer, density.Get()); },
    [&] {
      for (const auto option : densityOptions) {
        SelectableListItem(
            composer, BindListItem(Bind(density), option),
            [&] { ComposeOption(composer, option); }, {}, {},
            KeyFor(option));
      }
    },
    buttonProperties, popupProperties, "density");
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

Tabs<Page>(
    composer, Bind(page), definitions,
    [&](Composer &, const auto &tab, bool selected) {
      ComposeTabHeader(composer, tab.label, selected);
    },
    [&](Composer &, const auto &tab, bool) {
      ComposePage(composer, tab.value);
    },
    presentation, "settings-tabs");
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
MenuButton(composer, actionsMenu, "actions-button",
           [&] { ComposeButtonLabel(composer, "Actions"); },
           [&] {
             MenuItem(composer,
                      [&] { ComposeButtonLabel(composer, "Duplicate"); },
                      [&] {
                        DuplicateSelection();
                        actionsMenu.Close();
                      },
                      {}, "duplicate");
           });
```

Menu items are focusable, activate with pointer, Enter, or Space, and expose
`MenuItem` semantics. Up/Down and Home/End move through a menu; Escape and
outside-pointer dismissal are supplied by the popup.

For a context menu, attach the secondary-button handler to a target and compose
the popup beside it:

```cpp
AttachContextMenu(targetProperties, contextMenu);
composer.Border([&] { ComposeTarget(composer); }, targetProperties, "target");
ContextMenu(composer, contextMenu,
            [&] { ComposeContextItems(composer, contextMenu); });
```

The controller records the pointer position as the popup anchor. It must
outlive the composed handlers.

## Incremental Data Sources and Virtualization

`IIncrementalDataSource<T>` deliberately defines the boundary before
virtualization:

- `Count()` reports the logical item count;
- `Revision()` identifies a data revision;
- `ItemAt(index)` returns checked item access;
- `RequestRange({first, count})` asks the source to make a range available.

`VectorDataSource<T>` adapts an existing span for ordinary in-memory data.
`ListView` does **not** virtualize yet. The test suite includes a 5,000-item
non-virtualized performance guardrail so a later virtualization implementation
has a measured baseline and cannot silently weaken keyed identity, keyboard
navigation, or semantics.

See the buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) Collections page and
[`CollectionTests.cpp`](../../Packages/NGIN.UI/tests/CollectionTests.cpp) for
complete public-API examples.
