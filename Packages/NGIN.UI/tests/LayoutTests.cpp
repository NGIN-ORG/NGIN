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
  const auto measured =
      layout.Measure(columnNode->handle,
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
