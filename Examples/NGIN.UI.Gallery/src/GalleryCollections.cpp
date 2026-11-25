#include "GallerySamplePicker.hpp"

#include <array>
#include <utility>

namespace NGIN::UIGallery::Pages {
using namespace NGIN::UI;
using NGIN::Text::String;

namespace {
enum class CollectionSample { Large, Selection, Menus };

auto VirtualListCard(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  NodeProperties content{};
  content.layout.gap = theme.spacing.regular;

  std::vector<Views::View> virtualListContent{
      TextView(String{"100,000-item list"}, 20.0F, theme.colors.foreground,
               "virtual-list-title", SemanticRole::Heading),
      TextView(String{"Select a row, then add rows above it. The list "
                      "keeps your position."},
               theme.typography.body, theme.colors.mutedForeground,
               "virtual-list-help")};

  {
    std::vector<Views::View> virtualListActionsViews{ButtonView(
        theme, "Add 250 rows above",
        [&model] { model.PrependVirtualizedItems(); }, "prepend-virtual-items",
        182.0F)};
    const auto selected = model.SelectedVirtualizedIndex();
    virtualListActionsViews.push_back(
        TextView(selected ? LabeledNumber("Selected row: ", *selected + 1)
                          : String{"No row selected"},
                 theme.typography.body, theme.colors.mutedForeground,
                 "virtual-selected-row"));
    virtualListContent.push_back(
        ActionRow(std::move(virtualListActionsViews), "virtual-list-actions"));
  }

  auto &source = model.VirtualizedCollectionSource();
  auto &controller = model.VirtualizedCollectionController();
  VirtualizedListPresentation presentation{};
  presentation.list.layout.preferredSize.height = 268;
  presentation.list.layout.maximumSize = Size{680.0F, 280.0F};
  presentation.list.layout.horizontalAlignment = HorizontalAlignment::Stretch;
  presentation.list.layout.verticalAlignment = VerticalAlignment::Start;
  presentation.list.layout.padding = Thickness::Uniform(Dp{6.0F});
  presentation.list.visual = MakePanelVisual(theme);
  presentation.list.visual.base.background = theme.colors.sunkenSurface;
  presentation.list.semantics.label = String{"100,000-item virtual list"};
  StyleScrollView(presentation.list, theme);
  presentation.item.layout.padding = Thickness{12.0F, 8.0F, 12.0F, 8.0F};
  presentation.item.visual = MakePanelVisual(theme);
  presentation.item.visual.base.background = theme.colors.raisedSurface;
  presentation.item.visual.states.hovered.borderColor = theme.colors.focus;
  presentation.item.visual.states.pressed.background =
      theme.colors.accentPressed;
  presentation.item.visual.states.selected.background = theme.colors.accent;
  presentation.item.visual.states.selected.borderColor = theme.colors.focus;
  presentation.selectedIndex = [&model] {
    return model.SelectedVirtualizedIndex();
  };
  presentation.isSelected = [&model](const auto index) {
    return model.SelectedVirtualizedIndex() == index;
  };
  presentation.activate = [&model](const auto index) {
    return model.SelectVirtualizedItem(index);
  };
  presentation.onError = [&model](const UIError &error) {
    model.Report(error);
  };
  virtualListContent.push_back(
      Views::VirtualizedList<NGIN::UIntSize>(
          controller, source,
          [&model, &source, theme](const NGIN::UIntSize &,
                                   const NGIN::UIntSize index) {
            auto label = source.LabelAt(index);
            return TextView(label ? std::move(label).value()
                                  : String{"Loading row"},
                            theme.typography.body,
                            model.SelectedVirtualizedIndex() == index
                                ? theme.colors.accentForeground
                                : theme.colors.foreground,
                            "virtual-row-label");
          },
          presentation)
          .Key("virtual-list-100000"));

  virtualListContent.push_back(Views::Component([&controller, theme] {
    const auto diagnostics = controller.Diagnostics();
    String rangeLine{"Rows on screen: "};
    if (diagnostics.realized.count == 0) {
      rangeLine.Append(String{"none"});
    } else {
      rangeLine.Append(Number(diagnostics.realized.first + 1));
      rangeLine.Append(String{" to "});
      rangeLine.Append(Number(diagnostics.realized.End()));
    }
    rangeLine.Append(String{" · created: "});
    rangeLine.Append(Number(diagnostics.realizedNodeCount));
    rangeLine.Append(String{" · range loads: "});
    rangeLine.Append(Number(diagnostics.rangeRequestCount));
    return TextView(std::move(rangeLine), theme.typography.caption,
                    theme.colors.mutedForeground, "virtual-list-diagnostics");
  }));
  return CardView(theme,
                  {Views::Column(std::move(virtualListContent), content)
                       .Key("virtual-list-content")},
                  "virtual-list-card");
}

auto CollectionCard(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  NodeProperties collectionColumn{};
  collectionColumn.layout.gap = theme.spacing.regular;

  std::vector<Views::View> collectionContent{
      TextView(String{"Selectable list"}, 20.0F, theme.colors.foreground,
               "list-title", SemanticRole::Heading),
      TextView(String{"Use the arrow keys, Home, End, or type a letter."},
               theme.typography.body, theme.colors.mutedForeground,
               "list-help")};
  collectionContent.push_back(ActionRow(
      {ButtonView(
           theme, "Add item", [&model] { model.AddCollectionItem(); },
           "add-item", 98.0F),
       ButtonView(
           theme, "Remove selected",
           [&model] { model.RemoveSelectedCollectionItem(); }, "remove-item",
           142.0F, ButtonAppearance::Neutral),
       ButtonView(
           theme,
           model.IsCollectionDescending() ? "Sort ascending"
                                          : "Sort descending",
           [&model] { model.ToggleCollectionSort(); }, "sort-items", 142.0F, ButtonAppearance::Neutral),
       ButtonView(
           theme, model.IsCollectionFiltered() ? "Show all" : "Hide even items",
           [&model] { model.ToggleCollectionFilter(); }, "filter-items",
           152.0F, ButtonAppearance::Neutral)},
      "collection-actions"));
  NodeProperties list{};
  list.layout.preferredSize.height = 250;
  list.layout.maximumSize = Size{680.0F, 250.0F};
  list.layout.horizontalAlignment = HorizontalAlignment::Stretch;
  list.layout.verticalAlignment = VerticalAlignment::Start;
  list.layout.padding = Thickness::Uniform(Dp{6.0F});
  list.visual = MakePanelVisual(theme);
  list.visual.base.background = theme.colors.sunkenSurface;
  list.semantics.label = String{"Gallery items"};
  StyleScrollView(list, theme);
  const auto items = model.CollectionItems();
  const auto selected = model.SelectedCollectionItem();

  NodeProperties itemColumn{};
  itemColumn.layout.gap = theme.spacing.compact;

  std::vector<Views::View> keyedItemsViews;
  for (const auto item : items) {
    auto itemProperties = NodeProperties{};
    itemProperties.layout.preferredSize.height = 42;
    itemProperties.layout.padding = Thickness{12.0F, 8.0F, 12.0F, 8.0F};
    itemProperties.layout.horizontalAlignment = HorizontalAlignment::Stretch;
    itemProperties.layout.verticalAlignment = VerticalAlignment::Start;
    itemProperties.visual = MakePanelVisual(theme);
    itemProperties.visual.base.background = theme.colors.raisedSurface;
    itemProperties.visual.states.hovered.borderColor = theme.colors.focus;
    itemProperties.visual.states.pressed.background =
        theme.colors.accentPressed;
    itemProperties.visual.states.selected.background = theme.colors.accent;
    itemProperties.visual.states.selected.borderColor = theme.colors.focus;
    itemProperties.semantics.label = LabeledNumber("Item ", item);
    const auto key = std::to_string(item);
    keyedItemsViews.push_back(
        Views::SelectableListItem(
            model.CollectionSelection(item),
            TextView(LabeledNumber("Item ", item), 15.0F,
                     selected && *selected == item
                         ? theme.colors.accentForeground
                         : theme.colors.foreground,
                     "item-label"),
            itemProperties,
            CollectionPresentation{
                .onError =
                    [&model](const UIError &error) { model.Report(error); },
            })
            .Key(key));
  }
  collectionContent.push_back(
      Views::Box(ElementType::ListView,
                 {Views::Column(std::move(keyedItemsViews), itemColumn)
                      .Key("keyed-items")},
                 list)
          .Key("collection-list"));
  return CardView(theme,
                  {Views::Column(std::move(collectionContent), collectionColumn)
                       .Key("collection-content")},
                  "collection-card");
}

auto PopupPanel(const Theme &theme) -> NodeProperties {
  NodeProperties panel{};
  panel.layout.preferredSize.width = 204;
  panel.layout.padding = Thickness::Uniform(Dp{6});
  panel.layout.gap = 4;
  panel.visual = MakePanelVisual(theme);
  return panel;
}

auto PopupItem(const Theme &theme, const char *label) -> NodeProperties {
  NodeProperties item{};
  item.layout.minimumSize.height = 34;
  item.layout.padding = Thickness{10, 7, 10, 7};
  item.semantics.label = String{label};
  item.visual.base.background = theme.colors.surface;
  item.visual.states.hovered.background = theme.colors.raisedSurface;
  item.visual.states.selected.background = theme.colors.accent;
  item.visual.focus = MakeButtonVisual(theme).focus;
  return item;
}

auto TriggerStyle(const Theme &theme, const char *label) -> NodeProperties {
  NodeProperties trigger{};
  trigger.layout.minimumSize.height = theme.controls.regularHeight;
  trigger.layout.padding = Thickness{14, 8, 14, 8};
  trigger.visual = MakeButtonVisual(theme);
  trigger.semantics.label = String{label};
  return trigger;
}

auto DensityPicker(GalleryViewModel &model, const Theme &theme) -> Views::View {
  constexpr std::array options{Density::Compact, Density::Comfortable,
                               Density::Spacious};
  constexpr std::array labels{"Compact", "Comfortable", "Spacious"};
  std::vector<Views::View> items;
  String label{"Spacing · "};
  auto binding = model.DensityBinding();
  for (UIntSize index = 0; index < options.size(); ++index) {
    const bool selected = binding.Get() == options[index];
    if (selected)
      label.Append(String{labels[index]});
    auto selection = BindListItem(binding, options[index]);
    // Commit the selected value before dismissing the popup. Failed writes keep
    // the menu open.
    selection.select = [select = std::move(selection.select),
                        &model]() mutable -> UIResult<void> {
      auto result = select();
      if (result)
        model.ComboPopup().Close();
      return result;
    };
    items.push_back(Views::SelectableListItem(
                        std::move(selection),
                        TextView(labels[index], 14,
                                 selected ? theme.colors.accentForeground
                                          : theme.colors.foreground,
                                 "option-label"),
                        PopupItem(theme, labels[index]), {})
                        .Key(labels[index]));
  }
  return Views::ComboBox(model.ComboPopup(), "gallery-density-combo",
                         TextView(std::move(label), 14,
                                  theme.colors.accentForeground,
                                  "combo-summary"),
                         Views::Column(std::move(items), PopupPanel(theme))
                             .Key("density-options"),
                         TriggerStyle(theme, "Inspector spacing"), {})
      .Key("density-combo");
}

auto ItemActions(GalleryViewModel &model, const Theme &theme,
                 PopupController &popup) -> Views::View {
  const auto item = [&](const char *label, auto apply, const char *key) {
    return Views::MenuItem(
               TextView(label, 14, theme.colors.foreground, "action-label"),
               [&popup, apply] {
                 apply();
                 popup.Close();
               },
               PopupItem(theme, label))
        .Key(key);
  };
  return Views::Column(
             {item(
                  "Add item", [&model] { model.AddCollectionItem(); },
                  "menu-add"),
              item(
                  "Reverse order", [&model] { model.ToggleCollectionSort(); },
                  "menu-sort")},
             PopupPanel(theme))
      .Key("item-actions");
}

auto CollectionTabs(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  const std::array<TabDefinition<CollectionTab>, 3> tabs{
      TabDefinition<CollectionTab>{
          .value = CollectionTab::Selection,
          .key = String{"selection"},
          .label = String{"Selection"},
      },
      TabDefinition<CollectionTab>{
          .value = CollectionTab::Identity,
          .key = String{"identity"},
          .label = String{"Order"},
      },
      TabDefinition<CollectionTab>{
          .value = CollectionTab::DataSource,
          .key = String{"data-source"},
          .label = String{"Count"},
      },
  };
  TabsPresentation tabsPresentation{};
  tabsPresentation.root.layout.gap = theme.spacing.regular;
  tabsPresentation.root.layout.horizontalAlignment = HorizontalAlignment::Start;
  tabsPresentation.tabList.layout.gap = theme.spacing.compact;
  tabsPresentation.tab.layout.preferredSize =
      Size{84.0F, theme.controls.regularHeight};
  tabsPresentation.tab.layout.padding = Thickness{12.0F, 8.0F, 12.0F, 8.0F};
  tabsPresentation.tab.visual = MakePanelVisual(theme);
  tabsPresentation.tab.visual.states.hovered.background =
      theme.colors.raisedSurface;
  tabsPresentation.tab.visual.states.selected.background = theme.colors.accent;
  tabsPresentation.tab.visual.states.selected.foreground =
      theme.colors.accentForeground;
  tabsPresentation.tab.visual.focus = MakeButtonVisual(theme).focus;
  tabsPresentation.panel.layout.minimumSize.height = 48;
  tabsPresentation.panel.layout.padding = Thickness::Uniform(
      Dp{model.DensityBinding().Get() == Density::Compact       ? 6.0F
         : model.DensityBinding().Get() == Density::Comfortable ? 15.0F
                                                                : 24.0F});
  tabsPresentation.panel.visual = MakePanelVisual(theme);
  tabsPresentation.panel.visual.base.background = theme.colors.sunkenSurface;
  tabsPresentation.onError = [&model](const UIError &error) {
    model.Report(error);
  };
  AttachContextMenu(tabsPresentation.panel, model.ContextPopup());
  return Views::Tabs<CollectionTab>(
             model.CollectionTabBinding(), tabs,
             [theme](const auto &definition, const bool selectedTab) {
               return TextView(definition.label, 14.0F,
                               selectedTab ? theme.colors.accentForeground
                                           : theme.colors.foreground,
                               "tab-label");
             },
             [&model, theme](const auto &definition, const bool) {
               String value;
               switch (definition.value) {
               case CollectionTab::Selection:
                 if (auto id = model.SelectedCollectionItem())
                   value = LabeledNumber("Selected item ", *id);
                 else
                   value = String{"No item selected"};
                 break;
               case CollectionTab::Identity:
                 value =
                     String{model.IsCollectionDescending() ? "Descending order"
                                                           : "Ascending order"};
                 break;
               case CollectionTab::DataSource:
                 value = LabeledNumber("Visible items · ",
                                       model.CollectionItems().size());
                 break;
               }
               return TextView(std::move(value), 16, theme.colors.foreground,
                               "tab-description");
             },
             tabsPresentation)
      .Key("collection-tabs");
}

auto NavigationCard(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  auto menu =
      Views::MenuButton(model.MenuPopup(), "gallery-menu-button",
                        TextView("Item actions", 14,
                                 theme.colors.accentForeground, "menu-summary"),
                        ItemActions(model, theme, model.MenuPopup()),
                        TriggerStyle(theme, "Item actions"), {})
          .Key("actions-menu-button");
  return SampleCard(
      theme,
      {TextView("Collection inspector", 22, theme.colors.foreground,
                "navigation-title", SemanticRole::Heading),
       TextView("Use Item actions or right-click the selected tab’s value.", 14,
                theme.colors.mutedForeground, "navigation-help"),
       ActionRow({DensityPicker(model, theme), std::move(menu)},
                 "navigation-controls"),
       CollectionTabs(model, theme),
       Views::ContextMenu(model.ContextPopup(),
                          ItemActions(model, theme, model.ContextPopup()))},
      "navigation-content", "navigation-card");
}
} // namespace

auto CollectionsView(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    auto choice = scope.Local("sample", CollectionSample::Large);
    Views::View sample;
    switch (choice.Get()) {
    case CollectionSample::Large:
      sample = VirtualListCard(model, theme);
      break;
    case CollectionSample::Selection:
      sample = CollectionCard(model, theme);
      break;
    case CollectionSample::Menus:
      sample = NavigationCard(model, theme);
      break;
    }
    return PageContentView(
        {PageHeadingView(theme, "Collections",
                         "Browse a large list or edit a small collection."),
         SamplePicker(
             theme, choice, "Collection samples",
             {{CollectionSample::Large, "Virtual list", "collections-large"},
              {CollectionSample::Selection, "Selection",
               "collections-selection"},
              {CollectionSample::Menus, "Menus & tabs", "collections-menus"}}),
         std::move(sample)});
  });
}
} // namespace NGIN::UIGallery::Pages
