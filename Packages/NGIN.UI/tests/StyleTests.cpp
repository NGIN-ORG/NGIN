#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Resources.hpp>
#include <NGIN/UI/Theme.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <memory>

TEST_CASE("visual state styles resolve with deterministic precedence") {
  using namespace NGIN::UI;

  VisualProperties properties{};
  properties.base.background = Color{0.1F, 0.1F, 0.1F, 1.0F};
  properties.base.foreground = Color{0.9F, 0.9F, 0.9F, 1.0F};
  properties.states.hovered.background = Color{0.2F, 0.2F, 0.2F, 1.0F};
  properties.states.pressed.background = Color{0.3F, 0.3F, 0.3F, 1.0F};
  properties.states.focused.borderColor = Color{0.0F, 0.5F, 1.0F, 1.0F};
  properties.states.invalid.borderColor = Color{1.0F, 0.0F, 0.0F, 1.0F};
  properties.states.disabled.background = Color{0.05F, 0.05F, 0.05F, 1.0F};
  properties.states.disabled.foreground = Color{0.4F, 0.4F, 0.4F, 1.0F};

  const auto interactive = ResolveVisualStyle(
      properties, VisualStateFlags::Hovered | VisualStateFlags::Pressed |
                      VisualStateFlags::Focused | VisualStateFlags::Invalid);
  REQUIRE(interactive.background == properties.states.pressed.background);
  REQUIRE(interactive.borderColor == properties.states.focused.borderColor);

  const auto disabled = ResolveVisualStyle(
      properties, VisualStateFlags::Hovered | VisualStateFlags::Pressed |
                      VisualStateFlags::Disabled);
  REQUIRE(disabled.background == properties.states.disabled.background);
  REQUIRE(disabled.foreground == properties.states.disabled.foreground);
}

TEST_CASE("theme helpers honor palette and metric overrides") {
  using namespace NGIN::UI;

  auto theme = MakeLightTheme();
  theme.revision = 7;
  theme.colors.accent = Color{0.6F, 0.1F, 0.8F, 1.0F};
  theme.colors.focus = Color{1.0F, 0.4F, 0.0F, 1.0F};
  theme.controls.regularHeight = 44.0F;
  theme.controls.borderThickness = 3.0F;
  theme.radii.regular = 12.0F;

  auto resources = std::make_shared<ResourceScope>();
  resources->Provide(ThemeResource, theme);
  const auto *resolved = resources->Resolve(ThemeResource);
  REQUIRE(resolved != nullptr);
  REQUIRE(resolved->revision == 7);

  const auto button = MakeButtonVisual(*resolved);
  REQUIRE(button.base.background == theme.colors.accent);
  REQUIRE(button.base.borderThickness ==
          Thickness::Uniform(Dp{theme.controls.borderThickness}));
  REQUIRE(button.base.cornerRadius ==
          CornerRadius::Uniform(Dp{theme.radii.regular}));
  REQUIRE(button.focus.color == theme.colors.focus);
}

TEST_CASE("rounded chrome and focus visuals lower into DPI-aware geometry") {
  using namespace NGIN::UI;

  const Theme theme{};
  NodeProperties properties{};
  properties.layout.preferredSize = Size{100.0F, 40.0F};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.visual = MakeButtonVisual(theme);

  Composer composer;
  composer.Leaf(ElementType::Button, properties, "styled");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto handle = tree.Get(tree.Root())->children.front();
  tree.Get(handle)->interaction.focused = true;

  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{120.0F, 60.0F},
          .maximum = Size{120.0F, 60.0F},
      },
      Rect{0.0F, 0.0F, 120.0F, 60.0F}));

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 3);
  REQUIRE(std::holds_alternative<StrokeRoundedRect>(displayList[0]));
  REQUIRE(std::holds_alternative<FillRoundedRect>(displayList[1]));
  REQUIRE(std::holds_alternative<StrokeRoundedRect>(displayList[2]));
  REQUIRE(std::get<StrokeRoundedRect>(displayList[0]).rect ==
          Rect{-2.0F, -2.0F, 104.0F, 44.0F});

  const auto packet =
      UIRenderer{}.Build(displayList, PixelSize{240, 120}, 2.0F);
  REQUIRE(packet.scaleFactor == 2.0F);
  REQUIRE_FALSE(packet.vertices.empty());
  REQUIRE_FALSE(packet.indices.empty());
  REQUIRE(packet.batches.size() == 1);
}

TEST_CASE("per-edge borders and separator helpers remain structural") {
  using namespace NGIN::UI;

  NodeProperties border{};
  border.layout.preferredSize = Size{80.0F, 30.0F};
  border.layout.horizontalAlignment = HorizontalAlignment::Start;
  border.layout.verticalAlignment = VerticalAlignment::Start;
  border.visual.base.borderColor = Color{0.8F, 0.2F, 0.1F, 1.0F};
  border.visual.base.borderThickness = Thickness{1.0F, 2.0F, 3.0F, 4.0F};

  Composer composer;
  composer.Border([&] { composer.Leaf(ElementType::Rectangle, "child"); },
                  border, "panel");
  auto separatorVisual = MakeSeparatorVisual(Theme{});
  NodeProperties separator{};
  separator.visual = separatorVisual;
  composer.Separator(SeparatorOrientation::Horizontal, separator, "rule");

  REQUIRE(composer.Declarations().size() == 2);
  REQUIRE(composer.Declarations()[0].type == ElementType::Border);
  REQUIRE(composer.Declarations()[1].type == ElementType::Separator);
  REQUIRE(composer.Declarations()[1].properties.layout.preferredSize.height ==
          1.0F);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{100.0F, 50.0F},
          .maximum = Size{100.0F, 50.0F},
      },
      Rect{0.0F, 0.0F, 100.0F, 50.0F}));

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 5);
  for (NGIN::UIntSize index = 0; index < 5; ++index) {
    REQUIRE(std::holds_alternative<FillRect>(displayList[index]));
  }
}

TEST_CASE("styled chrome respects nested scroll clipping") {
  using namespace NGIN::UI;

  NodeProperties scroll{};
  scroll.layout.preferredSize = Size{50.0F, 20.0F};
  scroll.layout.maximumSize = Size{50.0F, 20.0F};
  scroll.layout.horizontalAlignment = HorizontalAlignment::Start;
  scroll.layout.verticalAlignment = VerticalAlignment::Start;
  scroll.scroll.horizontal = true;
  scroll.scroll.vertical = true;

  NodeProperties panel{};
  panel.layout.preferredSize = Size{100.0F, 60.0F};
  panel.layout.horizontalAlignment = HorizontalAlignment::Start;
  panel.layout.verticalAlignment = VerticalAlignment::Start;
  panel.visual = MakePanelVisual(Theme{});

  Composer composer;
  composer.ScrollView([&] { composer.Border([] {}, panel, "clipped-panel"); },
                      scroll, "viewport");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{100.0F, 40.0F},
          .maximum = Size{100.0F, 40.0F},
      },
      Rect{0.0F, 0.0F, 100.0F, 40.0F}));

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 8);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList[0]));
  REQUIRE(std::holds_alternative<FillRoundedRect>(displayList[1]));
  REQUIRE(std::holds_alternative<StrokeRoundedRect>(displayList[2]));
  REQUIRE(std::holds_alternative<PopClip>(displayList[3]));
  for (NGIN::UIntSize index = 4; index < 8; ++index) {
    REQUIRE(std::holds_alternative<FillRoundedRect>(displayList[index]));
  }

  const auto packet = UIRenderer{}.Build(displayList, PixelSize{200, 80}, 2.0F);
  REQUIRE(packet.batches.size() == 2);
  REQUIRE(packet.batches.front().scissor == PixelRect{0, 0, 100, 40});
  REQUIRE(packet.batches.back().scissor == PixelRect{0, 0, 200, 80});
}

TEST_CASE("hover press and disabled states change rendered control colors") {
  using namespace NGIN::UI;

  NodeProperties properties{};
  properties.layout.preferredSize = Size{80.0F, 30.0F};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.visual = MakeButtonVisual(Theme{});

  Composer composer;
  composer.Leaf(ElementType::Button, properties, "states");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto handle = tree.Get(tree.Root())->children.front();
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{100.0F, 40.0F},
          .maximum = Size{100.0F, 40.0F},
      },
      Rect{0.0F, 0.0F, 100.0F, 40.0F}));

  tree.Get(handle)->interaction.hovered = true;
  auto displayList = BuildDisplayList(tree);
  REQUIRE(std::get<FillRoundedRect>(displayList[0]).color ==
          *properties.visual.states.hovered.background);

  tree.Get(handle)->interaction.pressed = true;
  displayList = BuildDisplayList(tree);
  REQUIRE(std::get<FillRoundedRect>(displayList[0]).color ==
          *properties.visual.states.pressed.background);

  tree.Get(handle)->properties.interaction.enabled = false;
  displayList = BuildDisplayList(tree);
  REQUIRE(std::get<FillRoundedRect>(displayList[0]).color ==
          *properties.visual.states.disabled.background);
}
