#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/SplitPane.hpp>

namespace {
using namespace NGIN;
using namespace NGIN::UI;

class BottomRight final : public ILayoutPolicy {
public:
  auto Measure(MeasureChildren &children, SizeConstraints constraints)
      -> Size override {
    REQUIRE(children.Count() == 1);
    return children.Measure(0, {.maximum = constraints.maximum});
  }

  void Arrange(ArrangeChildren &children, Size size) override {
    REQUIRE(children.Count() == 1);
    const auto desired = children.DesiredSize(0);
    children.Place(0, {size.width - desired.width, size.height - desired.height,
                       desired.width, desired.height});
  }
};

TEST_CASE("custom container placement uses content coordinates for rendering "
          "and input") {
  Composer composer;
  NodeProperties parent{};
  parent.layout.padding = Thickness::Uniform(Dp{5});
  parent.layoutPolicy = std::make_shared<BottomRight>();
  NodeProperties child{};
  child.layout.preferredSize = {20, 10};
  child.semantics.identifier = Text::String{"child"};
  composer.Element(ElementType::Custom, parent,
                   [&] { composer.Leaf(ElementType::Rectangle, child); });
  RuntimeTree tree;
  Reconciler reconciler{tree};
  reconciler.Reconcile(composer.Declarations());
  LayoutEngine layout{tree};
  const Rect viewport{20, 10, 100, 40};
  layout.Perform({.minimum = {100, 40}, .maximum = {100, 40}}, viewport);
  const auto handle = tree.FindBySemanticIdentifier(Text::String{"child"});
  REQUIRE(handle);
  CHECK(tree.Get(handle)->arrangedBounds == Rect{95, 35, 20, 10});
  InputRouter input{tree};
  CHECK(input.HitTest({97, 37}) == handle);
  CHECK(input.HitTest({93, 37}) != handle);
}

TEST_CASE("split divider retains drag capture across recomposition and accepts "
          "keyboard and accessibility input") {
  State<F32> ratio{0.25F};
  Composer composer;
  NodeProperties parent{};
  parent.layout.padding = Thickness::Uniform(Dp{4});
  NodeProperties body{};
  body.semantics.identifier = Text::String{"body"};
  const auto compose = [&](Composer &out) {
    SplitPane(
        out, Bind(ratio), [&] { out.Leaf(ElementType::Rectangle, body); },
        [&] { out.Leaf(ElementType::Rectangle); }, parent, "split");
  };
  compose(composer);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter input{tree};
  reconciler.Reconcile(composer.Declarations());
  const auto arrange = [&](F32 width) {
    layout.Perform({.minimum = {width, 80}, .maximum = {width, 80}},
                   {10, 5, width, 80});
  };
  arrange(216);
  const auto bodyHandle = tree.FindBySemanticIdentifier(Text::String{"body"});
  REQUIRE(bodyHandle);
  CHECK(tree.Get(bodyHandle)->arrangedBounds == Rect{14, 9, 50, 72});
  const auto divider = input.HitTest({65, 30});
  REQUIRE(divider);
  CHECK(input
            .Route(PointerButtonChanged{.pointerId = 3,
                                        .button = PointerButton::Primary,
                                        .state = ButtonState::Pressed,
                                        .position = {65, 30}})
            .handled);
  CHECK(input.CapturedElement(3) == divider);
  CHECK(input.FocusedElement() == divider);

  Composer next;
  compose(next);
  reconciler.Reconcile(next.Declarations());
  input.Synchronize();
  arrange(216);
  auto move = input.Route(PointerMoved{.pointerId = 3, .position = {165, 30}});
  REQUIRE(move.handled);
  CHECK(HasInvalidation(move.invalidation, InvalidationKind::Measure));
  CHECK(ratio.Get() == Catch::Approx(0.75F));
  arrange(216);
  CHECK(tree.Get(bodyHandle)->arrangedBounds.width == Catch::Approx(150));
  CHECK(input.Route(PointerMoved{.pointerId = 3, .position = {-100, 30}})
            .handled);
  CHECK(ratio.Get() == 0);
  CHECK(input
            .Route(PointerButtonChanged{.pointerId = 3,
                                        .button = PointerButton::Primary,
                                        .state = ButtonState::Released,
                                        .position = {-100, 30}})
            .handled);
  CHECK_FALSE(input.CapturedElement(3));

  CHECK(input
            .Route(
                KeyChanged{.logicalKey = static_cast<UInt32>(LogicalKey::Right),
                           .state = KeyState::Pressed})
            .handled);
  CHECK(ratio.Get() == Catch::Approx(0.05F));
  const auto adjust = input.PerformSemanticAction(
      divider, {.action = SemanticActionKind::SetValue, .numericValue = 0.8});
  REQUIRE(adjust);
  CHECK(ratio.Get() == Catch::Approx(0.8F));
  arrange(116);
  CHECK(tree.Get(bodyHandle)->arrangedBounds.width == Catch::Approx(80));
  arrange(10);
  CHECK(tree.Get(bodyHandle)->arrangedBounds.width == 0);
}
class CountingText final : public ITextLayout, public IGlyphAtlas {
public:
  auto MetricsRevision() const noexcept -> UInt64 override { return metrics; }
  auto AtlasRevision() const noexcept -> UInt64 override { return atlas; }
  auto LayoutParagraph(const ParagraphRequest &) noexcept
      -> UIResult<ParagraphLayout> override {
    ++shaped;
    return ParagraphLayout{.size = {20, 14}};
  }
  auto ResolveGlyph(const GlyphAtlasRequest &) noexcept
      -> UIResult<GlyphAtlasEntry> override {
    return GlyphAtlasEntry{};
  }
  UInt64 metrics{1};
  UInt64 atlas{1};
  UIntSize shaped{};
};

TEST_CASE(
    "incremental layout reuses a static text panel across divider drags") {
  CountingText service;
  State<F32> ratio{0.5F};
  Composer content;
  content.Column(
      [&] {
        content.Text(Text::String{"Settings"}, service, service, {}, "title");
      },
      "independent-panel");
  SplitPane(
      content, Bind(ratio),
      [&] { content.Text(Text::String{"Left"}, service, service); },
      [&] { content.Leaf(ElementType::Rectangle); });
  RuntimeTree tree;
  Reconciler reconciler{tree};
  reconciler.Reconcile(content.Declarations());
  LayoutEngine engine{tree};
  const auto pass = [&](F32 width, F32 scale = 1.0F) {
    return engine.Perform({.minimum = {width, 80}, .maximum = {width, 80}},
                          {0, 0, width, 80}, scale, true);
  };
  pass(120);
  REQUIRE(service.shaped == 2);
  const auto title = tree.Get(tree.Get(tree.Root())->children[0])->children[0];
  const auto titleRevision = tree.Get(title)->layoutRevision;
  REQUIRE(ratio.Set(0.75F));
  const auto drag = pass(120);
  CHECK(service.shaped == 3);
  CHECK(drag.reusedMeasurements > 0);
  CHECK(drag.reusedPlacements > 0);
  CHECK(tree.Get(title)->layoutRevision == titleRevision);
  pass(200);
  CHECK(service.shaped == 5);
  pass(200, 2.0F);
  CHECK(service.shaped == 7);
  ++service.atlas;
  pass(200, 2.0F);
  CHECK(service.shaped == 9);
  ++service.metrics;
  pass(200, 2.0F);
  CHECK(service.shaped == 11);
  service.metrics = 0;
  pass(200, 2.0F);
  pass(200, 2.0F);
  CHECK(service.shaped == 15);
}

TEST_CASE(
    "reordered cached labels keep measurements and receive new geometry") {
  CountingText service;
  auto make = [&](const char *key) {
    NodeProperties properties{};
    properties.text.value = Text::String{key};
    properties.text.layout = &service;
    properties.text.glyphAtlas = &service;
    properties.semantics.identifier = Text::String{key};
    return std::make_shared<const ElementDeclaration>(ElementType::Text, key,
                                                      properties);
  };
  const auto first = make("first");
  const auto second = make("second");
  RuntimeTree tree;
  Reconciler reconcile{tree};
  LayoutEngine layout{tree};
  const auto build = [&](const auto &a, const auto &b) {
    Composer composer;
    composer.Column([&] {
      composer.AppendCached(a);
      composer.AppendCached(b);
    });
    reconcile.Reconcile(composer.Declarations());
    return layout.Perform({.minimum = {100, 80}, .maximum = {100, 80}},
                          {0, 0, 100, 80}, 1, true);
  };
  build(first, second);
  const auto firstHandle = tree.FindBySemanticIdentifier(Text::String{"first"});
  REQUIRE(firstHandle);
  REQUIRE(tree.Get(firstHandle)->arrangedBounds.y == 0);
  const auto stats = build(second, first);
  CHECK(tree.FindBySemanticIdentifier(Text::String{"first"}) == firstHandle);
  CHECK(tree.Get(firstHandle)->arrangedBounds.y == 14);
  CHECK(stats.reusedMeasurements == 2);
  CHECK(service.shaped == 2);
}

} // namespace
