#pragma once

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/State.hpp>

#include <algorithm>
#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace NGIN::UI {
/// @brief Selection behavior supported by a collection control.
enum class SelectionMode : UInt8 {
  None,
  Single,
  Multiple,
};

/// @brief Selection model that intentionally never selects an item.
template <typename T> class NoSelectionModel final {
public:
  [[nodiscard]] static constexpr auto Mode() noexcept -> SelectionMode {
    return SelectionMode::None;
  }
  [[nodiscard]] constexpr auto IsSelected(const T &) const noexcept -> bool {
    return false;
  }
  [[nodiscard]] constexpr auto Select(const T &) const noexcept -> bool {
    return false;
  }
  [[nodiscard]] constexpr auto Clear() const noexcept -> bool { return false; }
};

/// @brief Observable selection model that stores at most one value.
template <typename T>
  requires std::equality_comparable<T>
class SingleSelectionModel final {
public:
  explicit SingleSelectionModel(
      std::optional<T> initialValue = {}, InvalidationScheduler scheduler = {},
      const InvalidationKind invalidation = InvalidationKind::Compose |
                                            InvalidationKind::Paint)
      : m_value(std::move(initialValue), std::move(scheduler), invalidation) {}

  [[nodiscard]] static constexpr auto Mode() noexcept -> SelectionMode {
    return SelectionMode::Single;
  }
  [[nodiscard]] auto Value() const noexcept -> const std::optional<T> & {
    return m_value.Get();
  }
  [[nodiscard]] auto IsSelected(const T &item) const -> bool {
    return m_value.Get().has_value() && *m_value.Get() == item;
  }
  [[nodiscard]] auto Select(T item) -> bool {
    return m_value.Set(std::optional<T>{std::move(item)});
  }
  [[nodiscard]] auto Clear() -> bool { return m_value.Set(std::nullopt); }
  [[nodiscard]] auto ValueBinding() -> Binding<std::optional<T>> {
    return Bind(m_value);
  }

private:
  State<std::optional<T>> m_value;
};

/// @brief Observable selection model that stores a set of selected values.
template <typename T>
  requires std::equality_comparable<T>
class MultipleSelectionModel final {
public:
  explicit MultipleSelectionModel(
      std::vector<T> initialValue = {}, InvalidationScheduler scheduler = {},
      const InvalidationKind invalidation = InvalidationKind::Compose |
                                            InvalidationKind::Paint)
      : m_value(std::move(initialValue), std::move(scheduler), invalidation) {}

  [[nodiscard]] static constexpr auto Mode() noexcept -> SelectionMode {
    return SelectionMode::Multiple;
  }
  [[nodiscard]] auto Values() const noexcept -> const std::vector<T> & {
    return m_value.Get();
  }
  [[nodiscard]] auto IsSelected(const T &item) const -> bool {
    return std::find(m_value.Get().begin(), m_value.Get().end(), item) !=
           m_value.Get().end();
  }
  [[nodiscard]] auto Select(T item) -> bool {
    if (IsSelected(item)) {
      return false;
    }
    return m_value.Update([item = std::move(item)](auto &values) mutable {
      values.push_back(std::move(item));
    });
  }
  [[nodiscard]] auto Deselect(const T &item) -> bool {
    if (!IsSelected(item)) {
      return false;
    }
    return m_value.Update([&item](auto &values) { std::erase(values, item); });
  }
  [[nodiscard]] auto Toggle(T item) -> bool {
    return IsSelected(item) ? Deselect(item) : Select(std::move(item));
  }
  [[nodiscard]] auto Clear() -> bool { return m_value.Set(std::vector<T>{}); }
  [[nodiscard]] auto ValueBinding() -> Binding<std::vector<T>> {
    return Bind(m_value);
  }

private:
  State<std::vector<T>> m_value;
};

/// @brief Type-erased selection behavior attached to one collection item.
struct ItemSelection final {
  SelectionMode mode{SelectionMode::None};
  NGIN::Utilities::Callable<bool()> isSelected{};
  NGIN::Utilities::Callable<UIResult<void>()> select{};
};

template <typename T>
  requires std::equality_comparable<T>
[[nodiscard]] auto BindListItem(Binding<T> selection, T item) -> ItemSelection {
  return ItemSelection{
      .mode = SelectionMode::Single,
      .isSelected = [selection, item] { return selection.Get() == item; },
      .select = [selection, item = std::move(item)]() -> UIResult<void> {
        return selection.Set(item);
      },
  };
}

template <typename T>
[[nodiscard]] auto BindListItem(NoSelectionModel<T> &, T) -> ItemSelection {
  return ItemSelection{
      .mode = SelectionMode::None,
      .isSelected = [] { return false; },
      .select = [] -> UIResult<void> { return {}; },
  };
}

template <typename T>
  requires std::equality_comparable<T>
[[nodiscard]] auto BindListItem(SingleSelectionModel<T> &selection, T item)
    -> ItemSelection {
  return ItemSelection{
      .mode = SelectionMode::Single,
      .isSelected = [&selection, item] { return selection.IsSelected(item); },
      .select = [&selection, item = std::move(item)]() -> UIResult<void> {
        static_cast<void>(selection.Select(item));
        return {};
      },
  };
}

template <typename T>
  requires std::equality_comparable<T>
[[nodiscard]] auto BindListItem(MultipleSelectionModel<T> &selection, T item)
    -> ItemSelection {
  return ItemSelection{
      .mode = SelectionMode::Multiple,
      .isSelected = [&selection, item] { return selection.IsSelected(item); },
      .select = [&selection, item = std::move(item)]() -> UIResult<void> {
        static_cast<void>(selection.Toggle(item));
        return {};
      },
  };
}

/// @brief Shared error presentation used by collection helpers.
struct CollectionPresentation final {
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

template <typename ComposeChildren>
void SelectableListItem(Composer &composer, ItemSelection selection,
                        ComposeChildren &&composeChildren,
                        NodeProperties properties = {},
                        const CollectionPresentation &presentation = {},
                        std::string_view key = {}) {
  const auto selected = selection.isSelected && selection.isSelected();
  properties.interaction.onActivate = [selection = std::move(selection),
                                       onError =
                                           presentation.onError]() mutable {
    if (!selection.select) {
      return;
    }
    auto result = selection.select();
    if (!result && onError) {
      onError(result.Error());
    }
  };
  properties.semantics.role = SemanticRole::ListItem;
  properties.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Select |
      SemanticActionFlags::ScrollIntoView;
  if (selected) {
    properties.semantics.states |= SemanticStateFlags::Selected;
    properties.visual.state |= VisualStateFlags::Selected;
  }
  composer.ListItem(std::forward<ComposeChildren>(composeChildren), properties,
                    key);
}

/// @brief Half-open item range requested from an incremental data source.
struct IncrementalRange final {
  UIntSize first{0};
  UIntSize count{0};

  [[nodiscard]] constexpr auto End() const noexcept -> UIntSize {
    return first + count;
  }
};

/// @brief Pull-based collection source with revision and range-loading support.
template <typename T> class IIncrementalDataSource {
public:
  virtual ~IIncrementalDataSource() = default;

  [[nodiscard]] virtual auto Count() const noexcept -> UIntSize = 0;
  [[nodiscard]] virtual auto Revision() const noexcept -> UInt64 = 0;
  [[nodiscard]] virtual auto ItemAt(UIntSize index) const -> UIResult<T> = 0;
  virtual auto RequestRange(IncrementalRange range) -> UIResult<void> = 0;
};

/// @brief Non-owning incremental data source backed by a contiguous span.
template <typename T>
class VectorDataSource final : public IIncrementalDataSource<T> {
public:
  explicit VectorDataSource(std::span<const T> items,
                            const UInt64 revision = 0) noexcept
      : m_items(items), m_revision(revision) {}

  [[nodiscard]] auto Count() const noexcept -> UIntSize override {
    return m_items.size();
  }
  [[nodiscard]] auto Revision() const noexcept -> UInt64 override {
    return m_revision;
  }
  [[nodiscard]] auto ItemAt(const UIntSize index) const
      -> UIResult<T> override {
    if (index >= m_items.size()) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Data-source item index is out of range", "NGIN.UI",
                         "VectorDataSource::ItemAt");
    }
    return m_items[index];
  }
  auto RequestRange(const IncrementalRange range) -> UIResult<void> override {
    if (range.first > m_items.size() ||
        range.count > m_items.size() - range.first) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Data-source range is out of bounds", "NGIN.UI",
                         "VectorDataSource::RequestRange");
    }
    return {};
  }

private:
  std::span<const T> m_items;
  UInt64 m_revision{0};
};

/// @brief Observable open state and anchor shared by popup-based controls.
class PopupController final {
public:
  explicit PopupController(
      InvalidationScheduler scheduler = {},
      const InvalidationKind invalidation = InvalidationKind::All)
      : m_open(false, std::move(scheduler), invalidation) {}

  [[nodiscard]] auto IsOpen() const noexcept -> bool { return m_open.Get(); }
  void Open() { static_cast<void>(m_open.Set(true)); }
  void Close() { static_cast<void>(m_open.Set(false)); }
  void Toggle() { static_cast<void>(m_open.Set(!m_open.Get())); }
  void SetAnchor(const Rect anchor) noexcept { m_anchor = anchor; }
  [[nodiscard]] auto Anchor() const noexcept -> Rect { return m_anchor; }

private:
  State<bool> m_open;
  Rect m_anchor{};
};

template <typename ComposeSummary, typename ComposeOptions>
void ComboBox(Composer &composer, PopupController &controller,
              std::string_view anchorIdentifier,
              ComposeSummary &&composeSummary, ComposeOptions &&composeOptions,
              NodeProperties buttonProperties = {},
              NodeProperties popupProperties = {}, std::string_view key = {}) {
  buttonProperties.interaction.focusable = true;
  buttonProperties.interaction.onActivate = [&controller] {
    controller.Toggle();
  };
  auto previousKey = buttonProperties.interaction.onKey;
  buttonProperties.interaction.onKey =
      [previousKey = std::move(previousKey),
       &controller](RoutedKeyEvent &event) mutable {
        if (previousKey) {
          previousKey(event);
        }
        if (!event.handled && event.state == KeyState::Pressed &&
            (event.logicalKey == LogicalKey::Down ||
             event.logicalKey == LogicalKey::Up)) {
          controller.Open();
          event.handled = true;
        }
      };
  buttonProperties.semantics.role = SemanticRole::ComboBox;
  buttonProperties.semantics.identifier = NGIN::Text::String{anchorIdentifier};
  buttonProperties.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Focus |
      SemanticActionFlags::Expand | SemanticActionFlags::Collapse;
  if (controller.IsOpen()) {
    buttonProperties.semantics.states |= SemanticStateFlags::Expanded;
  }

  composer.Element(
      ElementType::Overlay,
      [&] {
        composer.Element(ElementType::Button, buttonProperties,
                         std::forward<ComposeSummary>(composeSummary),
                         "anchor");
        if (controller.IsOpen()) {
          popupProperties.popup.anchorIdentifier =
              NGIN::Text::String{anchorIdentifier};
          popupProperties.popup.placement = PopupPlacement::BelowStart;
          popupProperties.popup.modal = false;
          popupProperties.popup.dismissOnOutsidePointer = true;
          popupProperties.popup.dismissOnEscape = true;
          popupProperties.popup.onDismiss = [&controller] {
            controller.Close();
          };
          popupProperties.semantics.role = SemanticRole::Group;
          composer.Popup(
              [&] {
                NodeProperties optionList{};
                optionList.interaction.focusable = true;
                optionList.scroll.vertical = false;
                optionList.scroll.horizontal = false;
                optionList.scroll.showScrollbars = false;
                optionList.semantics.role = SemanticRole::List;
                composer.ListView(std::forward<ComposeOptions>(composeOptions),
                                  optionList, "options");
              },
              popupProperties, "popup");
        }
      },
      key);
}

/// @brief Stable value, key, and label describing one tab.
template <typename T> struct TabDefinition final {
  T value{};
  NGIN::Text::String key{};
  NGIN::Text::String label{};
};

/// @brief Element properties and error handling used by the Tabs helper.
struct TabsPresentation final {
  NodeProperties root{};
  NodeProperties tabList{};
  NodeProperties tab{};
  NodeProperties panel{};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

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
                auto properties = presentation.tab;
                properties.interaction.focusable = true;
                properties.interaction.onActivate =
                    [selection, value = definition.value,
                     onError = presentation.onError]() {
                      auto result = selection.Set(value);
                      if (!result && onError) {
                        onError(result.Error());
                      }
                    };
                properties.semantics.role = SemanticRole::Tab;
                properties.semantics.label = definition.label;
                properties.semantics.actions =
                    SemanticActionFlags::Activate |
                    SemanticActionFlags::Focus |
                    SemanticActionFlags::Select |
                    SemanticActionFlags::ScrollIntoView;
                if (selected) {
                  properties.semantics.states |= SemanticStateFlags::Selected;
                  properties.visual.state |= VisualStateFlags::Selected;
                  properties.interaction.tabIndex = 0;
                } else {
                  properties.interaction.tabIndex = -1;
                }
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
                auto properties = presentation.panel;
                properties.visibility = selected ? ElementVisibility::Visible
                                                 : ElementVisibility::Collapsed;
                properties.semantics.role = SemanticRole::TabPanel;
                properties.semantics.label = definition.label;
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
  buttonProperties.interaction.focusable = true;
  buttonProperties.interaction.onActivate = [&controller] {
    controller.Toggle();
  };
  auto previousKey = buttonProperties.interaction.onKey;
  buttonProperties.interaction.onKey =
      [previousKey = std::move(previousKey),
       &controller](RoutedKeyEvent &event) mutable {
        if (previousKey) {
          previousKey(event);
        }
        if (!event.handled && event.state == KeyState::Pressed &&
            event.logicalKey == LogicalKey::Down) {
          controller.Open();
          event.handled = true;
        }
      };
  buttonProperties.semantics.role = SemanticRole::Button;
  buttonProperties.semantics.identifier = NGIN::Text::String{anchorIdentifier};
  if (controller.IsOpen()) {
    buttonProperties.semantics.states |= SemanticStateFlags::Expanded;
  }
  composer.Element(
      ElementType::Overlay,
      [&] {
        composer.Element(ElementType::Button, buttonProperties,
                         std::forward<ComposeSummary>(composeSummary),
                         "anchor");
        if (controller.IsOpen()) {
          popupProperties.popup.anchorIdentifier =
              NGIN::Text::String{anchorIdentifier};
          popupProperties.popup.placement = PopupPlacement::BelowStart;
          popupProperties.popup.modal = false;
          popupProperties.popup.dismissOnOutsidePointer = true;
          popupProperties.popup.dismissOnEscape = true;
          popupProperties.popup.onDismiss = [&controller] {
            controller.Close();
          };
          popupProperties.semantics.role = SemanticRole::Menu;
          composer.Popup(std::forward<ComposeMenu>(composeMenu),
                         popupProperties, "popup");
        }
      },
      key);
}

inline void AttachContextMenu(NodeProperties &target,
                              PopupController &controller) {
  auto previous = target.interaction.onPointer;
  target.interaction.onPointer = [previous = std::move(previous), &controller](
                                     RoutedPointerEvent &event) mutable {
    if (previous) {
      previous(event);
    }
    if (event.phase == EventPhase::Target &&
        event.eventKind == RoutedPointerEventKind::ButtonPressed &&
        event.button == PointerButton::Secondary) {
      controller.SetAnchor(
          Rect{event.position.x, event.position.y, 1.0F, 1.0F});
      controller.Open();
      event.handled = true;
    }
  };
}

template <typename ComposeMenu>
void ContextMenu(Composer &composer, PopupController &controller,
                 ComposeMenu &&composeMenu, NodeProperties popupProperties = {},
                 std::string_view key = "context-menu") {
  if (!controller.IsOpen()) {
    return;
  }
  popupProperties.popup.anchor = controller.Anchor();
  popupProperties.popup.placement = PopupPlacement::BelowStart;
  popupProperties.popup.modal = false;
  popupProperties.popup.dismissOnOutsidePointer = true;
  popupProperties.popup.dismissOnEscape = true;
  popupProperties.popup.onDismiss = [&controller] { controller.Close(); };
  popupProperties.semantics.role = SemanticRole::Menu;
  composer.Popup(std::forward<ComposeMenu>(composeMenu), popupProperties, key);
}

template <typename ComposeChildren>
void MenuItem(Composer &composer, ComposeChildren &&composeChildren,
              NGIN::Utilities::Callable<void()> onActivate,
              NodeProperties properties = {}, std::string_view key = {}) {
  properties.interaction.focusable = true;
  properties.interaction.onActivate = std::move(onActivate);
  properties.semantics.role = SemanticRole::MenuItem;
  properties.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Focus;
  composer.MenuItem(std::forward<ComposeChildren>(composeChildren), properties,
                    key);
}
} // namespace NGIN::UI
