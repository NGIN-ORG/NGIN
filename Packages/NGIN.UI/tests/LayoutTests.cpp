#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <array>
#include <cmath>
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

class FixedTextLayout final : public NGIN::UI::ITextLayout {
public:
  auto LayoutParagraph(const NGIN::UI::ParagraphRequest &request) noexcept
      -> NGIN::UI::UIResult<NGIN::UI::ParagraphLayout> override {
    maximumWidth = request.maximumWidth;
    text = request.runs.front().text;
    return NGIN::UI::ParagraphLayout{
        .size = NGIN::UI::Size{18.0F, 8.0F},
        .runs =
            {
                NGIN::UI::PositionedShapedRun{
                    .run =
                        NGIN::UI::ShapedRun{
                            .fontFace = NGIN::UI::FontFaceHandle{1, 1},
                            .direction = NGIN::UI::TextDirection::LeftToRight,
                            .glyphs =
                                {
                                    NGIN::UI::ShapedGlyph{
                                        .glyphIndex = 10,
                                        .clusterByteOffset = 0,
                                        .advance = NGIN::UI::Point{5.0F, 0.0F},
                                    },
                                    NGIN::UI::ShapedGlyph{
                                        .glyphIndex = 11,
                                        .clusterByteOffset = 1,
                                        .advance = NGIN::UI::Point{6.0F, 0.0F},
                                    },
                                },
                            .size = NGIN::UI::Size{11.0F, 8.0F},
                        },
                    .origin = NGIN::UI::Point{2.0F, 3.0F},
                    .fontSize = 16.0F,
                },
            },
    };
  }

  NGIN::F32 maximumWidth{0.0F};
  NGIN::Text::String text{};
};

class FixedGlyphAtlas final : public NGIN::UI::IGlyphAtlas {
public:
  auto ResolveGlyph(const NGIN::UI::GlyphAtlasRequest &request) noexcept
      -> NGIN::UI::UIResult<NGIN::UI::GlyphAtlasEntry> override {
    requests.push_back(request);
    const auto textureX = request.glyphIndex == 10 ? 0.1F : 0.3F;
    return NGIN::UI::GlyphAtlasEntry{
        .texture = NGIN::UI::TextureHandle{7, 1},
        .textureCoordinates = NGIN::UI::Rect{textureX, 0.2F, 0.1F, 0.2F},
        .size = NGIN::UI::Size{4.0F, 6.0F},
        .bearing = NGIN::UI::Point{1.0F, 2.0F},
    };
  }

  std::vector<NGIN::UI::GlyphAtlasRequest> requests{};
};

class AsciiGraphemeSegmenter final : public NGIN::UI::IGraphemeSegmenter {
public:
  auto Segment(const NGIN::Text::String &text) noexcept
      -> NGIN::UI::UIResult<std::vector<NGIN::UI::GraphemeCluster>> override {
    std::vector<NGIN::UI::GraphemeCluster> clusters;
    for (NGIN::UIntSize offset = 0; offset < text.Size(); ++offset) {
      clusters.push_back({.byteOffset = offset, .byteLength = 1});
    }
    return clusters;
  }
};

class FixedTextGeometry final : public NGIN::UI::ITextGeometry {
public:
  struct RangeRequest final {
    NGIN::UIntSize byteOffset{0};
    NGIN::UIntSize byteLength{0};
  };

  auto CaretRect(const NGIN::UI::ParagraphLayout &,
                 const NGIN::UIntSize byteOffset) noexcept
      -> NGIN::UI::UIResult<NGIN::UI::Rect> override {
    caretByteOffset = byteOffset;
    return NGIN::UI::Rect{static_cast<NGIN::F32>(byteOffset * 2), 1.0F, 1.0F,
                          8.0F};
  }

  auto RangeRects(const NGIN::UI::ParagraphLayout &,
                  const NGIN::UIntSize byteOffset,
                  const NGIN::UIntSize byteLength) noexcept
      -> NGIN::UI::UIResult<std::vector<NGIN::UI::Rect>> override {
    ranges.push_back({byteOffset, byteLength});
    return std::vector{
        NGIN::UI::Rect{static_cast<NGIN::F32>(byteOffset * 2), 1.0F,
                       static_cast<NGIN::F32>(byteLength * 3), 8.0F},
    };
  }

  NGIN::UIntSize caretByteOffset{0};
  std::vector<RangeRequest> ranges{};
};
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

TEST_CASE("row distributes free space and overflow through flex properties") {
  using namespace NGIN::UI;

  NodeProperties rowProperties{};
  rowProperties.layout.gap = 10.0F;
  ElementDeclaration row{ElementType::Row, "row", rowProperties};

  auto fixed = LeafProperties(40.0F, 20.0F);
  auto flexible = LeafProperties(100.0F, 20.0F);
  flexible.layout.flexGrow = 1.0F;
  flexible.layout.flexShrink = 1.0F;
  row.children.emplace_back(ElementType::Rectangle, "fixed", fixed);
  row.children.emplace_back(ElementType::Rectangle, "flexible", flexible);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{row};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 30.0F},
          .maximum = Size{200.0F, 30.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 30.0F}));
  const auto *rowNode = Child(tree, tree.Root(), 0);

  REQUIRE(Child(tree, rowNode->handle, 0)->arrangedBounds.width == 40.0F);
  REQUIRE(Child(tree, rowNode->handle, 1)->arrangedBounds.width == 150.0F);

  layout.Arrange(rowNode->handle, Rect{0.0F, 0.0F, 120.0F, 30.0F});
  REQUIRE(Child(tree, rowNode->handle, 0)->arrangedBounds.width == 40.0F);
  REQUIRE(Child(tree, rowNode->handle, 1)->arrangedBounds.width == 70.0F);
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
  REQUIRE(displayList.size() == 5);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList[0]));
  REQUIRE(std::holds_alternative<FillRect>(displayList[1]));
  REQUIRE(std::holds_alternative<PopClip>(displayList[2]));
  REQUIRE(std::holds_alternative<FillRoundedRect>(displayList[3]));
  REQUIRE(std::holds_alternative<FillRoundedRect>(displayList[4]));
  const auto packet =
      UIRenderer{}.Build(displayList, PixelSize{200, 100}, 1.0F);
  REQUIRE(packet.batches.front().scissor == PixelRect{0, 0, 100, 50});
}

TEST_CASE("grid resolves fixed automatic and weighted tracks with spans") {
  using namespace NGIN::UI;

  NodeProperties gridProperties{};
  gridProperties.grid.columns = {GridTrack::Fixed(50.0F), GridTrack::Auto(),
                                 GridTrack::Weighted(1.0F)};
  gridProperties.grid.rows = {GridTrack::Auto(), GridTrack::Weighted(1.0F)};
  gridProperties.grid.columnGap = 5.0F;
  gridProperties.grid.rowGap = 5.0F;
  ElementDeclaration grid{ElementType::Grid, "grid", gridProperties};

  auto label = LeafProperties(30.0F, 20.0F);
  label.gridPlacement = GridPlacement{.row = 0, .column = 1};
  grid.children.emplace_back(ElementType::Rectangle, "label", label);
  auto field = LeafProperties(10.0F, 10.0F, HorizontalAlignment::Stretch,
                              VerticalAlignment::Stretch);
  field.gridPlacement = GridPlacement{.row = 1, .column = 1, .columnSpan = 2};
  grid.children.emplace_back(ElementType::Rectangle, "field", field);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{grid};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  const auto stats =
      layout.Perform(SizeConstraints{.minimum = Size{200.0F, 100.0F},
                                     .maximum = Size{200.0F, 100.0F}},
                     Rect{0.0F, 0.0F, 200.0F, 100.0F});

  const auto *gridNode = Child(tree, tree.Root(), 0);
  REQUIRE(stats.grids.size() == 1);
  REQUIRE(stats.grids.front().columns ==
          std::vector<NGIN::F32>{50.0F, 30.0F, 110.0F});
  REQUIRE(stats.grids.front().rows == std::vector<NGIN::F32>{20.0F, 75.0F});
  CHECK(Child(tree, gridNode->handle, 0)->arrangedBounds ==
        Rect{55.0F, 0.0F, 30.0F, 20.0F});
  CHECK(Child(tree, gridNode->handle, 1)->arrangedBounds ==
        Rect{55.0F, 25.0F, 145.0F, 75.0F});
}

TEST_CASE("grid intrinsic sizing is deterministic inside scrolling content") {
  using namespace NGIN::UI;

  NodeProperties scrollProperties{};
  scrollProperties.layout.preferredSize = Size{160.0F, 50.0F};
  scrollProperties.layout.maximumSize = Size{160.0F, 50.0F};
  ElementDeclaration scroll{ElementType::ScrollView, "scroll",
                            scrollProperties};

  NodeProperties gridProperties{};
  gridProperties.grid.columns = {GridTrack::Auto(20.0F, 80.0F),
                                 GridTrack::Weighted(1.0F, 25.0F)};
  gridProperties.grid.rows = {GridTrack::Auto()};
  gridProperties.grid.columnGap = 4.0F;
  ElementDeclaration grid{ElementType::Grid, "grid", gridProperties};
  auto first = LeafProperties(120.0F, 24.0F);
  first.gridPlacement = GridPlacement{.column = 0};
  grid.children.emplace_back(ElementType::Rectangle, "first", first);
  auto second = LeafProperties(35.0F, 24.0F);
  second.gridPlacement = GridPlacement{.column = 1};
  grid.children.emplace_back(ElementType::Rectangle, "second", second);
  scroll.children.push_back(grid);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{scroll};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  const auto *initialScroll = Child(tree, tree.Root(), 0);
  const auto *initialGrid = Child(tree, initialScroll->handle, 0);
  const auto intrinsic = layout.Measure(
      initialGrid->handle,
      SizeConstraints{.maximum =
                          Size{std::numeric_limits<NGIN::F32>::infinity(),
                               std::numeric_limits<NGIN::F32>::infinity()}});
  CHECK(intrinsic == Size{119.0F, 24.0F});
  static_cast<void>(
      layout.Perform(SizeConstraints{.minimum = Size{160.0F, 50.0F},
                                     .maximum = Size{160.0F, 50.0F}},
                     Rect{0.0F, 0.0F, 160.0F, 50.0F}));

  const auto *scrollNode = Child(tree, tree.Root(), 0);
  const auto *gridNode = Child(tree, scrollNode->handle, 0);
  CHECK(gridNode->measuredSize == Size{160.0F, 24.0F});
  CHECK(scrollNode->scroll.contentSize == Size{160.0F, 24.0F});
  CHECK(gridNode->grid.resolvedColumns == std::vector<NGIN::F32>{80.0F, 76.0F});
}

TEST_CASE("wrap panel forms stable lines and skips collapsed items") {
  using namespace NGIN::UI;

  NodeProperties wrapProperties{};
  wrapProperties.wrapPanel.itemGap = 4.0F;
  wrapProperties.wrapPanel.lineGap = 6.0F;
  wrapProperties.wrapPanel.lineAlignment = WrapLineAlignment::Center;
  ElementDeclaration wrap{ElementType::WrapPanel, "wrap", wrapProperties};
  wrap.children.emplace_back(ElementType::Rectangle, "one",
                             LeafProperties(40.0F, 10.0F));
  wrap.children.emplace_back(ElementType::Rectangle, "two",
                             LeafProperties(40.0F, 12.0F));
  auto collapsed = LeafProperties(90.0F, 90.0F);
  collapsed.visibility = ElementVisibility::Collapsed;
  wrap.children.emplace_back(ElementType::Rectangle, "collapsed", collapsed);
  wrap.children.emplace_back(ElementType::Rectangle, "three",
                             LeafProperties(40.0F, 8.0F));

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{wrap};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  const auto stats =
      layout.Perform(SizeConstraints{.minimum = Size{100.0F, 40.0F},
                                     .maximum = Size{100.0F, 40.0F}},
                     Rect{0.0F, 0.0F, 100.0F, 40.0F}, 1.5F);

  const auto *wrapNode = Child(tree, tree.Root(), 0);
  REQUIRE(stats.wrapPanels.size() == 1);
  REQUIRE(stats.wrapPanels.front().lines.size() == 2);
  CHECK(stats.wrapPanels.front().lines[0].itemCount == 2);
  CHECK(stats.wrapPanels.front().lines[1].itemCount == 1);
  CHECK(Child(tree, wrapNode->handle, 0)->arrangedBounds ==
        Rect{8.0F, 0.0F, 40.0F, 10.0F});
  CHECK(Child(tree, wrapNode->handle, 1)->arrangedBounds ==
        Rect{52.0F, 0.0F, 40.0F, 12.0F});
  CHECK(Child(tree, wrapNode->handle, 2)->arrangedBounds == Rect{});
  CHECK(Child(tree, wrapNode->handle, 3)->arrangedBounds ==
        Rect{30.0F, 18.0F, 40.0F, 8.0F});
}

TEST_CASE("canvas positions bounded content and clips overflowing children") {
  using namespace NGIN::UI;

  NodeProperties canvasProperties{};
  canvasProperties.layout.preferredSize = Size{80.5F, 40.5F};
  canvasProperties.layout.horizontalAlignment = HorizontalAlignment::Start;
  canvasProperties.layout.verticalAlignment = VerticalAlignment::Start;
  canvasProperties.canvas.clipToBounds = true;
  ElementDeclaration canvas{ElementType::Canvas, "canvas", canvasProperties};
  auto box = LeafProperties(30.0F, 20.0F);
  box.canvasPlacement.offset = Point{65.25F, 25.25F};
  box.canvasPlacement.contributesToDesiredSize = false;
  box.paintsBackground = true;
  canvas.children.emplace_back(ElementType::Rectangle, "box", box);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array declarations{canvas};
  static_cast<void>(reconciler.Reconcile(declarations));
  LayoutEngine layout{tree};
  static_cast<void>(
      layout.Perform(SizeConstraints{.minimum = Size{120.0F, 80.0F},
                                     .maximum = Size{120.0F, 80.0F}},
                     Rect{0.0F, 0.0F, 120.0F, 80.0F}, 1.25F));

  const auto *canvasNode = Child(tree, tree.Root(), 0);
  CHECK(canvasNode->arrangedBounds == Rect{0.0F, 0.0F, 80.5F, 40.5F});
  CHECK(Child(tree, canvasNode->handle, 0)->arrangedBounds ==
        Rect{65.25F, 25.25F, 30.0F, 20.0F});
  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() >= 3);
  CHECK(std::holds_alternative<PushClipRect>(displayList[0]));
  CHECK(std::holds_alternative<FillRect>(displayList[1]));
  CHECK(std::holds_alternative<PopClip>(displayList[2]));
}

TEST_CASE("grid keeps keyed children across track and order changes") {
  using namespace NGIN::UI;

  NodeProperties properties{};
  properties.grid.columns = {GridTrack::Weighted(), GridTrack::Weighted()};
  ElementDeclaration first{ElementType::Grid, "grid", properties};
  auto left = LeafProperties(20.0F, 10.0F);
  left.gridPlacement.column = 0;
  auto right = LeafProperties(20.0F, 10.0F);
  right.gridPlacement.column = 1;
  first.children.emplace_back(ElementType::Rectangle, "left", left);
  first.children.emplace_back(ElementType::Rectangle, "right", right);

  RuntimeTree tree;
  Reconciler reconciler{tree};
  const std::array initial{first};
  static_cast<void>(reconciler.Reconcile(initial));
  const auto *firstGrid = Child(tree, tree.Root(), 0);
  const auto leftHandle = firstGrid->children[0];
  const auto rightHandle = firstGrid->children[1];

  properties.grid.columns = {GridTrack::Fixed(30.0F), GridTrack::Weighted()};
  ElementDeclaration changed{ElementType::Grid, "grid", properties};
  changed.children.emplace_back(ElementType::Rectangle, "right", right);
  changed.children.emplace_back(ElementType::Rectangle, "left", left);
  const std::array updated{changed};
  static_cast<void>(reconciler.Reconcile(updated));

  const auto *changedGrid = Child(tree, tree.Root(), 0);
  CHECK(changedGrid->children[0] == rightHandle);
  CHECK(changedGrid->children[1] == leftHandle);
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

TEST_CASE("text element measures and paints shaped atlas glyphs") {
  using namespace NGIN::UI;

  FixedTextLayout textLayout;
  FixedGlyphAtlas glyphAtlas;
  NodeProperties properties{};
  properties.layout.padding = Thickness::Uniform(Dp{1.0F});
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.text.fontSize = 16.0F;
  properties.text.color = Color{0.25F, 0.5F, 0.75F, 1.0F};

  Composer composer;
  composer.Text(NGIN::Text::String{"Hi"}, textLayout, glyphAtlas, properties);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto textHandle = tree.Get(tree.Root())->children.front();

  LayoutEngine layout{tree};
  const auto measured =
      layout.Measure(textHandle, SizeConstraints{
                                     .maximum = Size{50.0F, 20.0F},
                                 });
  REQUIRE(measured == Size{20.0F, 10.0F});
  REQUIRE(textLayout.maximumWidth == 48.0F);
  REQUIRE(textLayout.text == NGIN::Text::String{"Hi"});
  REQUIRE(glyphAtlas.requests.size() == 2);
  REQUIRE(glyphAtlas.requests[0].fontSize == 16.0F);
  REQUIRE(glyphAtlas.requests[0].scaleFactor == 1.0F);

  layout.Arrange(textHandle, Rect{10.0F, 20.0F, 20.0F, 10.0F});
  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 3);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList[0]));
  const auto &glyphs = std::get<DrawGlyphRun>(displayList[1]);
  REQUIRE(glyphs.atlas == TextureHandle{7, 1});
  REQUIRE(glyphs.color == properties.text.color);
  REQUIRE(glyphs.glyphs.size() == 2);
  REQUIRE(glyphs.glyphs[0].destination == Rect{14.0F, 26.0F, 4.0F, 6.0F});
  REQUIRE(glyphs.glyphs[1].destination == Rect{19.0F, 26.0F, 4.0F, 6.0F});
  REQUIRE(std::holds_alternative<PopClip>(displayList[2]));

  const auto semantics = BuildSemanticTree(tree);
  const auto *semantic = semantics.FindByOwner(tree.Get(textHandle)->id);
  REQUIRE(semantic != nullptr);
  REQUIRE(semantic->role == SemanticRole::Text);
  REQUIRE(semantic->value == NGIN::Text::String{"Hi"});
}

TEST_CASE("text field paints shaped selection composition and caret geometry") {
  using namespace NGIN::UI;

  FixedTextLayout textLayout;
  FixedGlyphAtlas glyphAtlas;
  FixedTextGeometry geometry;
  AsciiGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"Hi"}};
  NodeProperties properties{};
  properties.layout.padding = Thickness::Uniform(Dp{1.0F});
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.text.layout = &textLayout;
  properties.text.geometry = &geometry;
  properties.text.glyphAtlas = &glyphAtlas;
  properties.text.fontSize = 16.0F;
  properties.textField.caretWidth = 2.0F;

  Composer composer;
  composer.TextField(Bind(value), segmenter, properties);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto textFieldHandle = tree.Get(tree.Root())->children.front();
  auto *textField = tree.Get(textFieldHandle);
  REQUIRE(textField->textField.editing != nullptr);
  REQUIRE(textField->textField.editing
              ->UpdateComposition(NGIN::Text::String{"XY"}, 0, 1)
              .HasValue());
  textField->interaction.focused = true;

  LayoutEngine layout{tree};
  const auto measured =
      layout.Measure(textFieldHandle, SizeConstraints{
                                          .maximum = Size{50.0F, 20.0F},
                                      });
  REQUIRE(measured == Size{20.0F, 10.0F});
  REQUIRE(textLayout.text == NGIN::Text::String{"HiXY"});
  REQUIRE(geometry.ranges.size() == 2);
  REQUIRE(geometry.ranges[0].byteOffset == 2);
  REQUIRE(geometry.ranges[0].byteLength == 1);
  REQUIRE(geometry.ranges[1].byteOffset == 2);
  REQUIRE(geometry.ranges[1].byteLength == 2);
  REQUIRE(geometry.caretByteOffset == 3);

  layout.Arrange(textFieldHandle, Rect{10.0F, 20.0F, 20.0F, 10.0F});
  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 6);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList[0]));
  const auto &selection = std::get<FillRect>(displayList[1]);
  REQUIRE(selection.rect == Rect{15.0F, 22.0F, 3.0F, 8.0F});
  REQUIRE(selection.color == properties.textField.selectionColor);
  REQUIRE(std::holds_alternative<DrawGlyphRun>(displayList[2]));
  const auto &composition = std::get<FillRect>(displayList[3]);
  REQUIRE(composition.rect == Rect{15.0F, 28.0F, 6.0F, 2.0F});
  REQUIRE(composition.color == properties.textField.compositionColor);
  const auto &caret = std::get<FillRect>(displayList[4]);
  REQUIRE(caret.rect == Rect{17.0F, 22.0F, 2.0F, 8.0F});
  REQUIRE(caret.color == properties.textField.caretColor);
  REQUIRE(std::holds_alternative<PopClip>(displayList[5]));
}

TEST_CASE("password text field shapes a grapheme-count mask") {
  using namespace NGIN::UI;

  FixedTextLayout textLayout;
  FixedGlyphAtlas glyphAtlas;
  AsciiGraphemeSegmenter segmenter;
  State<NGIN::Text::String> value{NGIN::Text::String{"secret"}};
  NodeProperties properties{};
  properties.text.layout = &textLayout;
  properties.text.glyphAtlas = &glyphAtlas;
  properties.textField.password = true;

  Composer composer;
  composer.TextField(Bind(value), segmenter, properties);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto textFieldHandle = tree.Get(tree.Root())->children.front();

  LayoutEngine layout{tree};
  static_cast<void>(
      layout.Measure(textFieldHandle, SizeConstraints{
                                          .maximum = Size{100.0F, 20.0F},
                                      }));
  REQUIRE(textLayout.text ==
          NGIN::Text::String{"\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"
                             "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"});
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

TEST_CASE("UI renderer tessellates antialiased and batched solid rectangles") {
  using namespace NGIN::UI;

  DisplayList list{
      FillRect{Rect{0.0F, 0.0F, 10.0F, 20.0F}, Color{1.0F, 0.0F, 0.0F, 1.0F}},
      FillRect{Rect{10.0F, 0.0F, 5.0F, 20.0F}, Color{0.0F, 1.0F, 0.0F, 1.0F}},
  };
  UIRenderer renderer;
  const auto packet = renderer.Build(list, PixelSize{200, 100}, 2.0F);

  REQUIRE(packet.vertices.size() == 18);
  REQUIRE(packet.indices.size() == 72);
  REQUIRE(packet.batches.size() == 1);
  REQUIRE(packet.batches.front().indexCount == 72);
  REQUIRE(packet.vertices[0].x == 10.0F);
  REQUIRE(packet.vertices[0].y == 20.0F);
  REQUIRE(packet.vertices[1].x == 0.5F);
  REQUIRE(packet.vertices[1].y == 0.5F);
  REQUIRE(packet.vertices[5].color == 0U);
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

  REQUIRE(packet.vertices.size() == 9);
  REQUIRE(packet.indices.size() == 36);
  REQUIRE(packet.vertices[0].x == 30.0F);
  REQUIRE(packet.vertices[0].y == 56.0F);
  REQUIRE(packet.vertices[1].x == 24.5F);
  REQUIRE(packet.vertices[1].y == 48.5F);
  REQUIRE(packet.vertices[5].x == 23.5F);
  REQUIRE(packet.vertices[5].y == 47.5F);
  REQUIRE(packet.vertices[0].color == 0x40000040U);
  REQUIRE(packet.vertices[5].color == 0U);
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

  REQUIRE(packet.vertices.size() == 61);
  REQUIRE(packet.indices.size() == 258);
  REQUIRE(packet.batches.size() == 2);
  REQUIRE_FALSE(packet.batches[0].texture);
  REQUIRE(packet.batches[0].indexCount == 252);
  REQUIRE(packet.batches[1].texture == texture);
  REQUIRE(packet.batches[1].indexCount == 6);
  REQUIRE(packet.vertices.back().u == 0.0F);
  REQUIRE(packet.vertices.back().v == 1.0F);
}

TEST_CASE("UI renderer lowers glyph runs with atlas coordinates") {
  using namespace NGIN::UI;

  const TextureHandle atlas{9, 1};
  DisplayList list{
      DrawGlyphRun{
          .atlas = atlas,
          .glyphs =
              {
                  GlyphQuad{
                      .destination = Rect{2.0F, 3.0F, 4.0F, 5.0F},
                      .textureCoordinates = Rect{0.1F, 0.2F, 0.25F, 0.3F},
                  },
                  GlyphQuad{
                      .destination = Rect{7.0F, 3.0F, 6.0F, 5.0F},
                      .textureCoordinates = Rect{0.4F, 0.2F, 0.3F, 0.3F},
                  },
              },
          .color = Color{1.0F, 0.5F, 0.25F, 0.75F},
      },
  };

  const auto packet = UIRenderer{}.Build(list, PixelSize{100, 100}, 2.0F);
  REQUIRE(packet.vertices.size() == 8);
  REQUIRE(packet.indices.size() == 12);
  REQUIRE(packet.batches.size() == 1);
  REQUIRE(packet.batches.front().texture == atlas);
  REQUIRE(packet.batches.front().indexCount == 12);
  REQUIRE(packet.vertices[0].x == 4.0F);
  REQUIRE(packet.vertices[0].y == 6.0F);
  REQUIRE(packet.vertices[0].u == 0.1F);
  REQUIRE(packet.vertices[0].v == 0.2F);
  REQUIRE(packet.vertices[2].u == 0.35F);
  REQUIRE(packet.vertices[2].v == 0.5F);
  REQUIRE(packet.vertices[4].u == 0.4F);
  REQUIRE(packet.vertices[6].u == Catch::Approx(0.7F));
}

TEST_CASE("UI renderer snaps glyph quads but preserves image placement") {
  using namespace NGIN::UI;

  const TextureHandle texture{10, 1};
  const Rect destination{2.2F, 3.3F, 4.0F, 5.0F};
  const auto glyphPacket = UIRenderer{}.Build(
      DisplayList{
          DrawGlyphRun{
              .atlas = texture,
              .glyphs =
                  {
                      GlyphQuad{
                          .destination = destination,
                          .textureCoordinates = Rect{0.1F, 0.2F, 0.3F, 0.4F},
                      },
                  },
          },
      },
      PixelSize{100, 100}, 1.25F);
  REQUIRE(glyphPacket.vertices.size() == 4);
  for (const auto &vertex : glyphPacket.vertices) {
    CHECK(vertex.x == std::round(vertex.x));
    CHECK(vertex.y == std::round(vertex.y));
  }
  CHECK(glyphPacket.vertices[0].x == 3.0F);
  CHECK(glyphPacket.vertices[0].y == 4.0F);
  CHECK(glyphPacket.vertices[2].x == 8.0F);
  CHECK(glyphPacket.vertices[2].y == 10.0F);

  const auto imagePacket = UIRenderer{}.Build(
      DisplayList{
          DrawImage{
              .texture = texture,
              .destination = destination,
          },
      },
      PixelSize{100, 100}, 1.25F);
  REQUIRE(imagePacket.vertices.size() == 4);
  CHECK(imagePacket.vertices[0].x == Catch::Approx(2.75F));
  CHECK(imagePacket.vertices[0].y == Catch::Approx(4.125F));
}
