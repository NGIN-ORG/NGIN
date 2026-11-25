#pragma once

#include <NGIN/UI/Collections.hpp>
#include <NGIN/UI/Composer.hpp>

namespace NGIN::UI {
template <typename ComposeChildren>
void SelectableListItem(Composer &composer, ItemSelection selection,
                        ComposeChildren &&composeChildren,
                        NodeProperties properties = {},
                        const CollectionPresentation &presentation = {},
                        std::string_view key = {}) {
  properties = Detail::SelectedItemProperties(
      std::move(selection), std::move(properties), presentation);
  composer.ListItem(std::forward<ComposeChildren>(composeChildren), properties,
                    key);
}

/// @brief Composes only the viewport and overscan rows of a stable-key source.
template <typename T, typename ComposeItem>
void VirtualizedListView(Composer &composer,
                         FixedVirtualizedListController &controller,
                         IVirtualizedDataSource<T> &source,
                         ComposeItem &&composeItem,
                         VirtualizedListPresentation presentation = {},
                         std::string_view key = {}) {
  auto list = Detail::PrepareVirtualizedList(controller, source, presentation);
  composer.ListView(
      [&] {
        Detail::ForEachRealizedRow(
            controller, source, presentation,
            [&](auto &item, UIntSize index, const auto &itemKey,
                NodeProperties properties) {
              composer.ListItem(
                  [&] {
                    if (item)
                      composeItem(composer, item.value(), index);
                  },
                  properties, itemKey.View());
            });
      },
      list, key);
}

template <typename ComposeSummary, typename ComposeOptions>
void ComboBox(Composer &composer, PopupController &controller,
              std::string_view anchorIdentifier,
              ComposeSummary &&composeSummary, ComposeOptions &&composeOptions,
              NodeProperties buttonProperties = {},
              NodeProperties popupProperties = {}, std::string_view key = {}) {
  buttonProperties = Detail::PopupAnchorProperties(
      controller, anchorIdentifier, true, std::move(buttonProperties));
  composer.Element(
      ElementType::Overlay,
      [&] {
        composer.Element(ElementType::Button, buttonProperties,
                         std::forward<ComposeSummary>(composeSummary),
                         "anchor");
        if (controller.IsPresented()) {
          popupProperties.popup.anchorIdentifier =
              NGIN::Text::String{anchorIdentifier};
          popupProperties = Detail::PopupProperties(
              controller, std::move(popupProperties), SemanticRole::Group);
          composer.Popup(
              [&] {
                composer.ListView(std::forward<ComposeOptions>(composeOptions),
                                  Detail::ComboListProperties(), "options");
              },
              popupProperties, "popup");
        }
      },
      key);
}

template <typename T, typename ComposeHeader, typename ComposePage>
  requires std::equality_comparable<T>
void Tabs(Composer &composer, Binding<T> selection,
          std::span<const TabDefinition<T>> tabs, ComposeHeader &&composeHeader,
          ComposePage &&composePage, const TabsPresentation &presentation = {},
          std::string_view key = {}) {
  composer.Element(
      ElementType::Column, presentation.root,
      [&] {
        auto tabList = presentation.tabList;
        tabList.semantics.role = SemanticRole::TabList;
        composer.Element(
            ElementType::Row, tabList,
            [&] {
              for (const auto &definition : tabs) {
                const auto selected = selection.Get() == definition.value;
                auto properties = Detail::TabProperties(selection, definition,
                                                        selected, presentation);
                composer.Tab(
                    [&] { composeHeader(composer, definition, selected); },
                    properties, definition.key.View());
              }
            },
            "tab-list");

        composer.Element(
            ElementType::Overlay,
            [&] {
              for (const auto &definition : tabs) {
                const auto selected = selection.Get() == definition.value;
                auto properties = Detail::TabPanelProperties(
                    definition, selected, presentation);
                composer.Element(
                    ElementType::Custom, properties,
                    [&] { composePage(composer, definition, selected); },
                    definition.key.View());
              }
            },
            "tab-panels");
      },
      key);
}

template <typename ComposeSummary, typename ComposeMenu>
void MenuButton(Composer &composer, PopupController &controller,
                std::string_view anchorIdentifier,
                ComposeSummary &&composeSummary, ComposeMenu &&composeMenu,
                NodeProperties buttonProperties = {},
                NodeProperties popupProperties = {},
                std::string_view key = {}) {
  buttonProperties = Detail::PopupAnchorProperties(
      controller, anchorIdentifier, false, std::move(buttonProperties));
  composer.Element(
      ElementType::Overlay,
      [&] {
        composer.Element(ElementType::Button, buttonProperties,
                         std::forward<ComposeSummary>(composeSummary),
                         "anchor");
        if (controller.IsPresented()) {
          popupProperties.popup.anchorIdentifier =
              NGIN::Text::String{anchorIdentifier};
          popupProperties = Detail::PopupProperties(
              controller, std::move(popupProperties), SemanticRole::Menu);
          composer.Popup(std::forward<ComposeMenu>(composeMenu),
                         popupProperties, "popup");
        }
      },
      key);
}

template <typename ComposeMenu>
void ContextMenu(Composer &composer, PopupController &controller,
                 ComposeMenu &&composeMenu, NodeProperties popupProperties = {},
                 std::string_view key = "context-menu") {
  if (!controller.IsPresented()) {
    return;
  }
  popupProperties.popup.anchor = controller.Anchor();
  popupProperties = Detail::PopupProperties(
      controller, std::move(popupProperties), SemanticRole::Menu);
  composer.Popup(std::forward<ComposeMenu>(composeMenu), popupProperties, key);
}

template <typename ComposeChildren>
void MenuItem(Composer &composer, ComposeChildren &&composeChildren,
              NGIN::Utilities::Callable<void()> onActivate,
              NodeProperties properties = {}, std::string_view key = {}) {
  properties =
      Detail::MenuItemProperties(std::move(onActivate), std::move(properties));
  composer.MenuItem(std::forward<ComposeChildren>(composeChildren), properties,
                    key);
}
} // namespace NGIN::UI
