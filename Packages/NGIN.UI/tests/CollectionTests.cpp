#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Collections.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Semantics.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace NGIN::UI;

[[nodiscard]] auto FindByType(const RuntimeTree &tree, const ElementType type,
                              const ElementHandle start = {}) -> ElementHandle {
  std::vector<ElementHandle> pending{
      start ? start : tree.Root(),
  };
  while (!pending.empty()) {
    const auto handle = pending.back();
    pending.pop_back();
    const auto *node = tree.Get(handle);
    if (node == nullptr) {
      continue;
    }
    if (node->type == type) {
      return handle;
    }
    for (const auto child : node->children) {
      pending.push_back(child);
    }
  }
  return {};
}

[[nodiscard]] auto FindByKey(const RuntimeTree &tree,
                             const std::string_view key) -> ElementHandle {
  std::vector<ElementHandle> pending{tree.Root()};
  while (!pending.empty()) {
    const auto handle = pending.back();
    pending.pop_back();
    const auto *node = tree.Get(handle);
    if (node == nullptr) {
      continue;
    }
    if (node->key.View() == key) {
      return handle;
    }
    for (const auto child : node->children) {
      pending.push_back(child);
    }
  }
  return {};
}

[[nodiscard]] auto FindByTypeAndKey(const RuntimeTree &tree,
                                    const ElementType type,
                                    const std::string_view key)
    -> ElementHandle {
  std::vector<ElementHandle> pending{tree.Root()};
  while (!pending.empty()) {
    const auto handle = pending.back();
    pending.pop_back();
    const auto *node = tree.Get(handle);
    if (node == nullptr) {
      continue;
    }
    if (node->type == type && node->key.View() == key) {
      return handle;
    }
    for (const auto child : node->children) {
      pending.push_back(child);
    }
  }
  return {};
}

auto ItemProperties(const char *label) -> NodeProperties {
  NodeProperties properties{};
  properties.layout.preferredSize = Size{120.0F, 30.0F};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.semantics.label = NGIN::Text::String{label};
  return properties;
}
} // namespace

TEST_CASE("typed selection models cover none single and multiple") {
  NoSelectionModel<int> none;
  REQUIRE(none.Mode() == SelectionMode::None);
  REQUIRE_FALSE(none.Select(4));
  REQUIRE_FALSE(none.IsSelected(4));

  SingleSelectionModel<int> single;
  REQUIRE(single.Mode() == SelectionMode::Single);
  REQUIRE_FALSE(single.Value().has_value());
  REQUIRE(single.Select(4));
  REQUIRE(single.IsSelected(4));
  REQUIRE_FALSE(single.IsSelected(8));
  REQUIRE(single.Clear());

  MultipleSelectionModel<int> multiple{{2}};
  REQUIRE(multiple.Mode() == SelectionMode::Multiple);
  REQUIRE(multiple.IsSelected(2));
  REQUIRE(multiple.Select(4));
  REQUIRE(multiple.Toggle(2));
  REQUIRE_FALSE(multiple.IsSelected(2));
  REQUIRE(multiple.Values() == std::vector<int>{4});
  REQUIRE(multiple.Clear());
}

TEST_CASE(
    "list view preserves keyed item identity through collection changes") {
  RuntimeTree tree;
  Reconciler reconciler{tree};
  SingleSelectionModel<int> selection{2};

  const auto compose = [&](const std::vector<int> &items) {
    Composer composer;
    NodeProperties list{};
    list.interaction.focusable = true;
    composer.ListView(
        [&] {
          composer.Column([&] {
            for (const auto item : items) {
              const auto key = std::to_string(item);
              SelectableListItem(
                  composer, BindListItem(selection, item), [] {},
                  ItemProperties(key.c_str()), {}, key);
            }
          });
        },
        list, "list");
    return reconciler.Reconcile(composer.Declarations());
  };

  auto stats = compose({1, 2, 3});
  REQUIRE(stats.created == 5);
  const auto one = FindByKey(tree, "1");
  const auto two = FindByKey(tree, "2");
  const auto three = FindByKey(tree, "3");
  const auto oneId = tree.Get(one)->id;
  const auto threeId = tree.Get(three)->id;

  stats = compose({4, 3, 1});
  REQUIRE(stats.created == 1);
  REQUIRE(stats.removed == 1);
  REQUIRE(tree.Get(FindByKey(tree, "1"))->id == oneId);
  REQUIRE(tree.Get(FindByKey(tree, "3"))->id == threeId);
  REQUIRE_FALSE(tree.IsAlive(two));

  stats = compose({3, 1});
  REQUIRE(stats.created == 0);
  REQUIRE(stats.removed == 1);
  REQUIRE(tree.Get(FindByKey(tree, "1"))->id == oneId);
  REQUIRE(tree.Get(FindByKey(tree, "3"))->id == threeId);
}

TEST_CASE("list view supports pointer keyboard type ahead and ensure visible") {
  RuntimeTree tree;
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter input{tree};
  SingleSelectionModel<int> selection{0};
  constexpr std::array labels{"Alpha", "Beta", "Charlie", "Delta", "Echo"};

  const auto compose = [&] {
    Composer composer;
    NodeProperties list{};
    list.layout.preferredSize = Size{120.0F, 70.0F};
    list.layout.maximumSize = Size{120.0F, 70.0F};
    list.layout.horizontalAlignment = HorizontalAlignment::Start;
    list.layout.verticalAlignment = VerticalAlignment::Start;
    list.interaction.focusable = true;
    list.scroll.vertical = true;
    composer.ListView(
        [&] {
          composer.Column([&] {
            for (NGIN::UIntSize index = 0; index < labels.size(); ++index) {
              SelectableListItem(
                  composer, BindListItem(selection, static_cast<int>(index)),
                  [] {}, ItemProperties(labels[index]), {},
                  std::to_string(index));
            }
          });
        },
        list, "list");
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    input.Synchronize();
    static_cast<void>(layout.Perform(
        SizeConstraints{
            .minimum = Size{120.0F, 70.0F},
            .maximum = Size{120.0F, 70.0F},
        },
        Rect{0.0F, 0.0F, 120.0F, 70.0F}));
  };

  compose();
  const auto list = FindByType(tree, ElementType::ListView);
  REQUIRE(input.SetFocus(list));

  auto result = input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::End),
      .state = KeyState::Pressed,
  }});
  REQUIRE(result.handled);
  REQUIRE(result.activated);
  REQUIRE(selection.IsSelected(4));
  REQUIRE(result.layoutStateChanged);
  REQUIRE(tree.Get(list)->scroll.offset.y > 0.0F);

  compose();
  result = input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>('c'),
      .state = KeyState::Pressed,
  }});
  REQUIRE(result.handled);
  REQUIRE(selection.IsSelected(2));

  static_cast<void>(input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Home),
      .state = KeyState::Pressed,
  }}));
  compose();
  const auto beta = FindByKey(tree, "1");
  const auto betaBounds = tree.Get(beta)->arrangedBounds;
  static_cast<void>(input.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 7,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{betaBounds.x + 4.0F, betaBounds.y + 4.0F},
  }}));
  result = input.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 7,
      .button = PointerButton::Primary,
      .state = ButtonState::Released,
      .position = Point{betaBounds.x + 4.0F, betaBounds.y + 4.0F},
  }});
  REQUIRE(result.activated);
  REQUIRE(selection.IsSelected(1));
  REQUIRE(input.FocusedElement() == list);

  compose();
  const auto semantics = BuildSemanticTree(tree);
  const auto *semanticList = semantics.FindByOwner(tree.Get(list)->id);
  REQUIRE(semanticList != nullptr);
  REQUIRE(semanticList->role == SemanticRole::List);
}

TEST_CASE("tabs retain collapsed page identity and traverse with arrows") {
  RuntimeTree tree;
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter input{tree};
  State<int> selected{0};
  const std::array<TabDefinition<int>, 2> tabs{
      TabDefinition<int>{
          .value = 0,
          .key = NGIN::Text::String{"first"},
          .label = NGIN::Text::String{"First"},
      },
      TabDefinition<int>{
          .value = 1,
          .key = NGIN::Text::String{"second"},
          .label = NGIN::Text::String{"Second"},
      },
  };

  const auto compose = [&] {
    Composer composer;
    TabsPresentation presentation{};
    presentation.root.layout.preferredSize = Size{200.0F, 100.0F};
    presentation.tab.layout.preferredSize = Size{80.0F, 30.0F};
    presentation.panel.layout.preferredSize = Size{200.0F, 70.0F};
    Tabs<int>(
        composer, Bind(selected), tabs,
        [](Composer &target, const auto &, const bool) {
          target.Leaf(ElementType::Spacer, "header");
        },
        [](Composer &target, const auto &definition, const bool) {
          std::string leafKey{definition.key.View()};
          leafKey += "-leaf";
          target.Leaf(ElementType::Spacer, leafKey);
        },
        presentation, "tabs");
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    input.Synchronize();
    static_cast<void>(layout.Perform(
        SizeConstraints{
            .minimum = Size{200.0F, 100.0F},
            .maximum = Size{200.0F, 100.0F},
        },
        Rect{0.0F, 0.0F, 200.0F, 100.0F}));
  };

  compose();
  const auto firstPageLeaf = FindByKey(tree, "first-leaf");
  const auto firstPageId = tree.Get(firstPageLeaf)->id;
  const auto firstTab = FindByTypeAndKey(tree, ElementType::Tab, "first");
  REQUIRE(input.SetFocus(firstTab));

  const auto result = input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Right),
      .state = KeyState::Pressed,
  }});
  REQUIRE(result.handled);
  REQUIRE(selected.Get() == 1);
  compose();
  REQUIRE(tree.Get(FindByKey(tree, "first-leaf"))->id == firstPageId);

  const auto firstPanel = tree.Get(firstPageLeaf)->parent;
  REQUIRE(tree.Get(firstPanel)->properties.visibility ==
          ElementVisibility::Collapsed);
  REQUIRE(tree.Get(firstPanel)->arrangedBounds == Rect{});
  const auto semantics = BuildSemanticTree(tree);
  REQUIRE(semantics.FindByOwner(tree.Get(firstPanel)->id) == nullptr);
}

TEST_CASE("combo box and menus use anchored popup foundations") {
  RuntimeTree tree;
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter input{tree};
  PopupController combo;
  PopupController context;

  const auto compose = [&] {
    Composer composer;
    NodeProperties comboButton{};
    comboButton.layout.preferredSize = Size{100.0F, 30.0F};
    comboButton.layout.horizontalAlignment = HorizontalAlignment::Start;
    comboButton.layout.verticalAlignment = VerticalAlignment::Start;
    NodeProperties comboPopup{};
    ComboBox(
        composer, combo, "combo-anchor", [] {},
        [&] {
          NodeProperties options{};
          options.layout.preferredSize = Size{90.0F, 60.0F};
          composer.Border([] {}, options, "options");
        },
        comboButton, comboPopup, "combo");

    NodeProperties contextTarget{};
    contextTarget.layout.preferredSize = Size{100.0F, 40.0F};
    contextTarget.layout.horizontalAlignment = HorizontalAlignment::Start;
    contextTarget.layout.verticalAlignment = VerticalAlignment::End;
    AttachContextMenu(contextTarget, context);
    composer.Border([] {}, contextTarget, "context-target");
    ContextMenu(composer, context, [&] {
      NodeProperties item{};
      item.layout.preferredSize = Size{80.0F, 30.0F};
      MenuItem(
          composer, [] {}, [&context] { context.Close(); }, item, "menu-item");
    });

    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    input.Synchronize();
    static_cast<void>(layout.Perform(
        SizeConstraints{
            .minimum = Size{200.0F, 120.0F},
            .maximum = Size{200.0F, 120.0F},
        },
        Rect{0.0F, 0.0F, 200.0F, 120.0F}));
  };

  compose();
  auto anchor =
      tree.FindBySemanticIdentifier(NGIN::Text::String{"combo-anchor"});
  REQUIRE(input.SetFocus(anchor));
  const auto opened = input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Down),
      .state = KeyState::Pressed,
  }});
  REQUIRE(opened.handled);
  REQUIRE(combo.IsOpen());
  compose();
  anchor = tree.FindBySemanticIdentifier(NGIN::Text::String{"combo-anchor"});
  const auto popup = FindByType(tree, ElementType::Popup);
  REQUIRE(anchor);
  REQUIRE(popup);
  REQUIRE(tree.Get(popup)->popup.contentBounds.y >=
          tree.Get(anchor)->arrangedBounds.y +
              tree.Get(anchor)->arrangedBounds.height);
  const auto semantics = BuildSemanticTree(tree);
  const auto *comboSemantic = semantics.FindByOwner(tree.Get(anchor)->id);
  REQUIRE(comboSemantic != nullptr);
  REQUIRE(comboSemantic->role == SemanticRole::ComboBox);
  REQUIRE(
      HasSemanticState(comboSemantic->states, SemanticStateFlags::Expanded));

  combo.Close();
  compose();
  const auto target = FindByKey(tree, "context-target");
  const auto bounds = tree.Get(target)->arrangedBounds;
  const auto result = input.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 3,
      .button = PointerButton::Secondary,
      .state = ButtonState::Pressed,
      .position = Point{bounds.x + 8.0F, bounds.y + 8.0F},
  }});
  REQUIRE(result.handled);
  REQUIRE(context.IsOpen());
  REQUIRE(context.Anchor().x == bounds.x + 8.0F);
}

TEST_CASE("menu items traverse with arrows and activate from keyboard") {
  RuntimeTree tree;
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter input{tree};
  PopupController menu;
  int activated = -1;

  const auto compose = [&] {
    Composer composer;
    NodeProperties button{};
    button.layout.preferredSize = Size{100.0F, 30.0F};
    button.layout.horizontalAlignment = HorizontalAlignment::Start;
    button.layout.verticalAlignment = VerticalAlignment::Start;
    MenuButton(
        composer, menu, "menu-anchor", [] {},
        [&] {
          NodeProperties column{};
          composer.Element(
              ElementType::Column, column,
              [&] {
                for (int index = 0; index < 3; ++index) {
                  NodeProperties item{};
                  item.layout.preferredSize = Size{90.0F, 30.0F};
                  MenuItem(
                      composer, [] {}, [&, index] { activated = index; }, item,
                      std::to_string(index));
                }
              },
              "items");
        },
        button, {}, "menu");
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    input.Synchronize();
    static_cast<void>(layout.Perform(
        SizeConstraints{
            .minimum = Size{200.0F, 160.0F},
            .maximum = Size{200.0F, 160.0F},
        },
        Rect{0.0F, 0.0F, 200.0F, 160.0F}));
  };

  menu.Open();
  compose();
  REQUIRE(input.FocusedElement() ==
          FindByTypeAndKey(tree, ElementType::MenuItem, "0"));
  auto moved = input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Down),
      .state = KeyState::Pressed,
  }});
  REQUIRE(moved.handled);
  REQUIRE(input.FocusedElement() ==
          FindByTypeAndKey(tree, ElementType::MenuItem, "1"));
  static_cast<void>(input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Enter),
      .state = KeyState::Pressed,
  }}));
  const auto invoked = input.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Enter),
      .state = KeyState::Released,
  }});
  REQUIRE(invoked.activated);
  REQUIRE(activated == 1);
}

TEST_CASE("incremental data source validates requested boundaries") {
  constexpr std::array values{3, 5, 8};
  VectorDataSource<int> source{values, 7};
  REQUIRE(source.Count() == 3);
  REQUIRE(source.Revision() == 7);
  REQUIRE(source.ItemAt(1).Value() == 5);
  REQUIRE_FALSE(source.ItemAt(3));
  REQUIRE(source.RequestRange(IncrementalRange{.first = 1, .count = 2}));
  REQUIRE_FALSE(source.RequestRange(IncrementalRange{.first = 2, .count = 2}));
}

TEST_CASE("large non-virtualized list has an explicit performance guardrail") {
  constexpr NGIN::UIntSize itemCount = 5000;
  RuntimeTree tree;
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  Composer composer;
  NodeProperties list{};
  list.layout.preferredSize = Size{320.0F, 480.0F};
  list.layout.maximumSize = Size{320.0F, 480.0F};
  list.interaction.focusable = true;
  composer.ListView(
      [&] {
        composer.Column([&] {
          for (NGIN::UIntSize index = 0; index < itemCount; ++index) {
            NodeProperties item{};
            item.layout.preferredSize = Size{300.0F, 24.0F};
            item.semantics.label = NGIN::Text::String{"Item"};
            composer.ListItem([] {}, item, std::to_string(index));
          }
        });
      },
      list, "large-list");

  const auto started = std::chrono::steady_clock::now();
  const auto stats = reconciler.Reconcile(composer.Declarations());
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{320.0F, 480.0F},
          .maximum = Size{320.0F, 480.0F},
      },
      Rect{0.0F, 0.0F, 320.0F, 480.0F}));
  const auto elapsed = std::chrono::steady_clock::now() - started;

  REQUIRE(stats.created == itemCount + 2);
  REQUIRE(tree.LiveCount() == itemCount + 3);
  REQUIRE(elapsed < std::chrono::seconds{5});
}
