#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <array>
#include <limits>

namespace {
[[nodiscard]] auto Child(const NGIN::UI::RuntimeTree &tree,
                         const NGIN::UI::ElementHandle parent,
                         const NGIN::UIntSize index)
    -> const NGIN::UI::RuntimeNode * {
  const auto *node = tree.Get(parent);
  REQUIRE(node != nullptr);
  REQUIRE(index < node->children.size());
  return tree.Get(node->children[index]);
}

[[nodiscard]] auto
LeafProperties(const NGIN::F32 width, const NGIN::F32 height,
               const NGIN::UI::HorizontalAlignment horizontal =
                   NGIN::UI::HorizontalAlignment::Start,
               const NGIN::UI::VerticalAlignment vertical =
                   NGIN::UI::VerticalAlignment::Start)
    -> NGIN::UI::NodeProperties {
  NGIN::UI::NodeProperties properties{};
  properties.layout.preferredSize = NGIN::UI::Size{width, height};
  properties.layout.horizontalAlignment = horizontal;
  properties.layout.verticalAlignment = vertical;
  return properties;
}
} // namespace

TEST_CASE("column measures natural size with padding and gaps") {
  using namespace NGIN::UI;

  NodeProperties columnProperties{};
  columnProperties.layout.padding = Thickness::Uniform(Dp{10.0F});
  columnProperties.layout.gap = 5.0F;

  ElementDeclaration column{ElementType::Column, "column", columnProperties};
  column.children.emplace_back(ElementType::Rectangle, "one",
                               LeafProperties(20.0F, 10.0F));
  column.children.emplace_back(ElementType::Rectangle, "two",
                               LeafProperties(40.0F, 20.0F));

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{column};
  reconciler.Reconcile(declarations);

  LayoutEngine layout{tree};
  const auto *columnNode = Child(tree, tree.Root(), 0);
  const auto measured = layout.Measure(
      columnNode->handle,
      SizeConstraints{
          .maximum = Size{std::numeric_limits<NGIN::F32>::infinity(),
                          std::numeric_limits<NGIN::F32>::infinity()},
      });

  REQUIRE(measured == Size{60.0F, 55.0F});
}

TEST_CASE("column arrange applies padding gap and cross-axis alignment") {
  using namespace NGIN::UI;

  NodeProperties columnProperties{};
  columnProperties.layout.padding = Thickness::Uniform(Dp{10.0F});
  columnProperties.layout.gap = 5.0F;

  ElementDeclaration column{ElementType::Column, "column", columnProperties};
  column.children.emplace_back(ElementType::Rectangle, "one",
                               LeafProperties(20.0F, 10.0F));
  column.children.emplace_back(
      ElementType::Rectangle, "two",
      LeafProperties(40.0F, 20.0F, HorizontalAlignment::Center));

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{column};
  reconciler.Reconcile(declarations);

  LayoutEngine layout{tree};
  const auto stats =
      layout.Perform(SizeConstraints{.minimum = Size{100.0F, 100.0F},
                                     .maximum = Size{100.0F, 100.0F}},
                     Rect{0.0F, 0.0F, 100.0F, 100.0F});

  const auto *columnNode = Child(tree, tree.Root(), 0);
  REQUIRE(columnNode->arrangedBounds == Rect{0.0F, 0.0F, 100.0F, 100.0F});
  REQUIRE(Child(tree, columnNode->handle, 0)->arrangedBounds ==
          Rect{10.0F, 10.0F, 20.0F, 10.0F});
  REQUIRE(Child(tree, columnNode->handle, 1)->arrangedBounds ==
          Rect{30.0F, 25.0F, 40.0F, 20.0F});
  REQUIRE(stats.measured == 4);
  REQUIRE(stats.arranged == 4);
}

TEST_CASE("row uses measured widths and vertical alignment") {
  using namespace NGIN::UI;

  NodeProperties rowProperties{};
  rowProperties.layout.gap = 4.0F;
  ElementDeclaration row{ElementType::Row, "row", rowProperties};
  row.children.emplace_back(ElementType::Rectangle, "one",
                            LeafProperties(10.0F, 10.0F,
                                           HorizontalAlignment::Start,
                                           VerticalAlignment::Center));
  row.children.emplace_back(ElementType::Rectangle, "two",
                            LeafProperties(20.0F, 20.0F,
                                           HorizontalAlignment::Start,
                                           VerticalAlignment::End));

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{row};
  reconciler.Reconcile(declarations);
  LayoutEngine layout{tree};
  layout.Perform(SizeConstraints{.minimum = Size{100.0F, 40.0F},
                                 .maximum = Size{100.0F, 40.0F}},
                 Rect{0.0F, 0.0F, 100.0F, 40.0F});

  const auto *rowNode = Child(tree, tree.Root(), 0);
  REQUIRE(Child(tree, rowNode->handle, 0)->arrangedBounds ==
          Rect{0.0F, 15.0F, 10.0F, 10.0F});
  REQUIRE(Child(tree, rowNode->handle, 1)->arrangedBounds ==
          Rect{14.0F, 20.0F, 20.0F, 20.0F});
}

TEST_CASE(
    "scroll view measures content unbounded and arranges retained offset") {
  using namespace NGIN::UI;

  NodeProperties scrollProperties{};
  scrollProperties.layout.preferredSize = Size{100.0F, 50.0F};
  scrollProperties.layout.maximumSize = Size{100.0F, 50.0F};
  scrollProperties.layout.horizontalAlignment = HorizontalAlignment::Start;
  scrollProperties.layout.verticalAlignment = VerticalAlignment::Start;
  ElementDeclaration scroll{ElementType::ScrollView, "scroll",
                            scrollProperties};

  auto contentProperties = LeafProperties(100.0F, 240.0F);
  contentProperties.paintsBackground = true;
  contentProperties.background = Color{0.2F, 0.4F, 0.8F, 1.0F};
  scroll.children.emplace_back(ElementType::Rectangle, "content",
                               contentProperties);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{scroll};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 100.0F},
          .maximum = Size{200.0F, 100.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 100.0F}));

  auto *scrollNode = tree.Get(tree.Get(tree.Root())->children.front());
  REQUIRE(scrollNode->arrangedBounds == Rect{0.0F, 0.0F, 100.0F, 50.0F});
  REQUIRE(scrollNode->scroll.viewportSize == Size{100.0F, 50.0F});
  REQUIRE(scrollNode->scroll.contentSize == Size{100.0F, 240.0F});
  REQUIRE(Child(tree, scrollNode->handle, 0)->arrangedBounds ==
          Rect{0.0F, 0.0F, 100.0F, 240.0F});

  scrollNode->scroll.offset.y = 80.0F;
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 100.0F},
          .maximum = Size{200.0F, 100.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 100.0F}));
  REQUIRE(Child(tree, scrollNode->handle, 0)->arrangedBounds ==
          Rect{0.0F, -80.0F, 100.0F, 240.0F});

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 3);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList[0]));
  REQUIRE(std::holds_alternative<FillRect>(displayList[1]));
  REQUIRE(std::holds_alternative<PopClip>(displayList[2]));
  const auto packet =
      UIRenderer{}.Build(displayList, PixelSize{200, 100}, 1.0F);
  REQUIRE(packet.batches.front().scissor == PixelRect{0, 0, 100, 50});
}

TEST_CASE(
    "popup placement flips within the viewport and paints above content") {
  using namespace NGIN::UI;

  NodeProperties popupProperties{};
  popupProperties.popup.anchor = Rect{150.0F, 80.0F, 20.0F, 10.0F};
  popupProperties.popup.placement = PopupPlacement::BelowEnd;
  popupProperties.popup.gap = 4.0F;
  ElementDeclaration popup{ElementType::Popup, "popup", popupProperties};

  auto popupContent = LeafProperties(60.0F, 20.0F);
  popupContent.paintsBackground = true;
  popupContent.background = Color{1.0F, 0.0F, 0.0F, 1.0F};
  popup.children.emplace_back(ElementType::Rectangle, "popup-content",
                              popupContent);

  auto page = LeafProperties(200.0F, 100.0F);
  page.paintsBackground = true;
  page.background = Color{0.0F, 0.0F, 1.0F, 1.0F};
  const std::array declarations{
      popup,
      ElementDeclaration{ElementType::Rectangle, "page", page},
  };

  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 100.0F},
          .maximum = Size{200.0F, 100.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 100.0F}));

  const auto *popupNode = Child(tree, tree.Root(), 0);
  REQUIRE(popupNode->arrangedBounds == Rect{0.0F, 0.0F, 200.0F, 100.0F});
  REQUIRE(Child(tree, popupNode->handle, 0)->arrangedBounds ==
          Rect{110.0F, 56.0F, 60.0F, 20.0F});

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 2);
  REQUIRE(std::get<FillRect>(displayList[0]).color == page.background);
  REQUIRE(std::get<FillRect>(displayList[1]).color == popupContent.background);
}

TEST_CASE("display-list builder diagnoses unbalanced scopes") {
  using namespace NGIN::UI;

  DisplayListBuilder underflow;
  auto popped = underflow.PopClip();
  REQUIRE_FALSE(popped.HasValue());
  REQUIRE(popped.Error().code == UIErrorCode::InvalidState);

  DisplayListBuilder unbalanced;
  unbalanced.PushClip(Rect{0.0F, 0.0F, 10.0F, 10.0F});
  auto unfinished = std::move(unbalanced).Finish();
  REQUIRE_FALSE(unfinished.HasValue());

  DisplayListBuilder balanced;
  balanced.PushClip(Rect{0.0F, 0.0F, 10.0F, 10.0F});
  balanced.Fill(Rect{1.0F, 1.0F, 2.0F, 2.0F}, Color{1.0F, 0.0F, 0.0F, 1.0F});
  REQUIRE(balanced.PopClip().HasValue());
  auto finished = std::move(balanced).Finish();
  REQUIRE(finished.HasValue());
  REQUIRE(finished.Value().size() == 3);
}

TEST_CASE("UI renderer tessellates and batches solid rectangles") {
  using namespace NGIN::UI;

  DisplayList list{
      FillRect{Rect{0.0F, 0.0F, 10.0F, 20.0F}, Color{1.0F, 0.0F, 0.0F, 1.0F}},
      FillRect{Rect{10.0F, 0.0F, 5.0F, 20.0F}, Color{0.0F, 1.0F, 0.0F, 1.0F}},
  };
  UIRenderer renderer;
  const auto packet = renderer.Build(list, PixelSize{200, 100}, 2.0F);

  REQUIRE(packet.vertices.size() == 8);
  REQUIRE(packet.indices.size() == 12);
  REQUIRE(packet.batches.size() == 1);
  REQUIRE(packet.batches.front().indexCount == 12);
  REQUIRE(packet.vertices[1].x == 20.0F);
  REQUIRE(packet.vertices[2].y == 40.0F);
  REQUIRE(packet.batches.front().scissor == PixelRect{0, 0, 200, 100});
}

TEST_CASE("UI renderer applies nested transform clip and opacity state") {
  using namespace NGIN::UI;

  DisplayList list{
      PushTransform{
          .translateX = 10.0F,
          .translateY = 20.0F,
          .scaleX = 2.0F,
          .scaleY = 2.0F,
      },
      PushClipRect{Rect{0.0F, 0.0F, 5.0F, 5.0F}},
      BeginOpacityLayer{0.5F},
      FillRect{Rect{1.0F, 2.0F, 3.0F, 4.0F}, Color{1.0F, 0.0F, 0.0F, 0.5F}},
      EndOpacityLayer{},
      PopClip{},
      PopTransform{},
  };
  UIRenderer renderer;
  const auto packet = renderer.Build(list, PixelSize{200, 200}, 2.0F);

  REQUIRE(packet.vertices.size() == 4);
  REQUIRE(packet.indices.size() == 6);
  REQUIRE(packet.vertices[0].x == 24.0F);
  REQUIRE(packet.vertices[0].y == 48.0F);
  REQUIRE(packet.vertices[2].x == 36.0F);
  REQUIRE(packet.vertices[2].y == 64.0F);
  REQUIRE(packet.vertices[0].color == 0x40000040U);
  REQUIRE(packet.batches.front().scissor == PixelRect{20, 40, 20, 20});
}

TEST_CASE("UI renderer emits rounded stroke and textured image geometry") {
  using namespace NGIN::UI;

  const TextureHandle texture{5, 2};
  DisplayList list{
      FillRoundedRect{
          Rect{0.0F, 0.0F, 20.0F, 10.0F},
          CornerRadius::Uniform(Dp{3.0F}),
          Color{0.2F, 0.3F, 0.4F, 1.0F},
      },
      StrokeRect{
          Rect{2.0F, 2.0F, 16.0F, 8.0F},
          2.0F,
          Color{1.0F, 1.0F, 1.0F, 1.0F},
      },
      DrawImage{
          texture,
          Rect{20.0F, 0.0F, 10.0F, 10.0F},
          Color{1.0F, 1.0F, 1.0F, 1.0F},
      },
  };
  UIRenderer renderer;
  const auto packet = renderer.Build(list, PixelSize{100, 100}, 1.0F);

  REQUIRE(packet.vertices.size() == 49);
  REQUIRE(packet.indices.size() == 114);
  REQUIRE(packet.batches.size() == 2);
  REQUIRE_FALSE(packet.batches[0].texture);
  REQUIRE(packet.batches[0].indexCount == 108);
  REQUIRE(packet.batches[1].texture == texture);
  REQUIRE(packet.batches[1].indexCount == 6);
  REQUIRE(packet.vertices.back().u == 0.0F);
  REQUIRE(packet.vertices.back().v == 1.0F);
}
