#pragma once

#include <NGIN/UI/Collections.hpp>
#include <NGIN/UI/Components.hpp>

namespace NGIN::UI::Views {
/// @brief Builds viewport and overscan rows from a stable-key data source.
/// The source and controller must outlive the mounted list.
template <typename T, typename Row>
[[nodiscard]] auto
VirtualizedList(FixedVirtualizedListController &controller,
                IVirtualizedDataSource<T> &source, Row row,
                VirtualizedListPresentation presentation = {}) -> View {
  return Component([&controller, &source, row = std::move(row),
                    presentation = std::move(presentation)]() mutable {
    auto list =
        UI::Detail::PrepareVirtualizedList(controller, source, presentation);
    std::vector<View> rows;
    rows.reserve(controller.RealizedRange().count);
    UI::Detail::ForEachRealizedRow(
        controller, source, presentation,
        [&](const auto &item, UIntSize index, const auto &key,
            NodeProperties properties) {
          std::vector<View> content;
          if (item)
            content.push_back(row(item.value(), index));
          rows.push_back(Box(ElementType::ListItem, std::move(content),
                             std::move(properties))
                             .Key(key.View()));
        });
    return Box(ElementType::ListView, std::move(rows), std::move(list));
  });
}

/// @brief Binds a child view to the collection's selection model.
[[nodiscard]] inline auto
SelectableListItem(ItemSelection selection, View child,
                   NodeProperties properties = {},
                   CollectionPresentation presentation = {}) -> View {
  return Component([selection = std::move(selection), child = std::move(child),
                    properties = std::move(properties),
                    presentation = std::move(presentation)] {
    return Box(ElementType::ListItem, {child},
               UI::Detail::SelectedItemProperties(selection, properties,
                                                  presentation));
  });
}

namespace Detail {
inline auto PopupTrigger(PopupController &controller, std::string_view anchor,
                         View summary, View content, NodeProperties button,
                         NodeProperties popup, bool isCombo) -> View {
  return Component([&controller, anchor = std::string(anchor),
                    summary = std::move(summary), content = std::move(content),
                    button = std::move(button), popup = std::move(popup),
                    isCombo] {
    auto buttonProperties =
        UI::Detail::PopupAnchorProperties(controller, anchor, isCombo, button);
    std::vector<View> children{
        Box(ElementType::Button, {summary}, std::move(buttonProperties))
            .Key("anchor")};
    if (controller.IsPresented()) {
      auto properties = UI::Detail::PopupProperties(
          controller, popup,
          isCombo ? SemanticRole::Group : SemanticRole::Menu);
      properties.popup.anchorIdentifier = Text::String{anchor};
      children.push_back(
          Box(ElementType::Popup, {content}, std::move(properties))
              .Key("popup"));
    }
    return Box(ElementType::Overlay, std::move(children));
  });
}
} // namespace Detail

/// @brief Anchors list options to a summary. The controller must outlive the
/// view.
[[nodiscard]] inline auto ComboBox(PopupController &controller,
                                   std::string_view anchor, View summary,
                                   View options, NodeProperties button = {},
                                   NodeProperties popup = {}) -> View {
  auto list = Box(ElementType::ListView, {std::move(options)},
                  UI::Detail::ComboListProperties())
                  .Key("options");
  return Detail::PopupTrigger(controller, anchor, std::move(summary),
                              std::move(list), std::move(button),
                              std::move(popup), true);
}

/// @brief Anchors a menu to a button. The controller must outlive the view.
[[nodiscard]] inline auto MenuButton(PopupController &controller,
                                     std::string_view anchor, View summary,
                                     View menu, NodeProperties button = {},
                                     NodeProperties popup = {}) -> View {
  return Detail::PopupTrigger(controller, anchor, std::move(summary),
                              std::move(menu), std::move(button),
                              std::move(popup), false);
}

[[nodiscard]] inline auto MenuItem(View child,
                                   Utilities::Callable<void()> activate,
                                   NodeProperties properties = {}) -> View {
  return Box(ElementType::MenuItem, {std::move(child)},
             UI::Detail::MenuItemProperties(std::move(activate),
                                            std::move(properties)));
}

[[nodiscard]] inline auto ContextMenu(PopupController &controller, View content,
                                      NodeProperties properties = {}) -> View {
  return Component([&controller, content = std::move(content),
                    properties = std::move(properties)] {
           if (!controller.IsPresented())
             return View{};
           auto popup = UI::Detail::PopupProperties(controller, properties,
                                                    SemanticRole::Menu);
           popup.popup.anchor = controller.Anchor();
           return Box(ElementType::Popup, {content}, std::move(popup));
         })
      .Key("context-menu");
}

/// @brief Creates keyed headers and page views, retaining the state of
/// collapsed tabs.
template <typename T, typename Header, typename Page>
[[nodiscard]] auto
Tabs(Binding<T> selection, std::span<const TabDefinition<T>> definitions,
     Header header, Page page, TabsPresentation presentation = {}) -> View {
  std::vector<TabDefinition<T>> tabs(definitions.begin(), definitions.end());
  return Component([selection = std::move(selection), tabs = std::move(tabs),
                    header = std::move(header), page = std::move(page),
                    presentation = std::move(presentation)]() mutable {
    std::vector<View> headers, panels;
    headers.reserve(tabs.size());
    panels.reserve(tabs.size());
    for (const auto &tab : tabs) {
      const bool selected = selection.Get() == tab.value;
      headers.push_back(
          Box(ElementType::Tab, {header(tab, selected)},
              UI::Detail::TabProperties(selection, tab, selected, presentation))
              .Key(tab.key.View()));
      panels.push_back(
          Box(ElementType::Custom, {page(tab, selected)},
              UI::Detail::TabPanelProperties(tab, selected, presentation))
              .Key(tab.key.View()));
    }
    auto list = presentation.tabList;
    list.semantics.role = SemanticRole::TabList;
    return Column(
        {Row(std::move(headers), std::move(list)).Key("tab-list"),
         Box(ElementType::Overlay, std::move(panels)).Key("tab-panels")},
        presentation.root);
  });
}
} // namespace NGIN::UI::Views
