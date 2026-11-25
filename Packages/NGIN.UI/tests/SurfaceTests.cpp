#include <NGIN/UI/SurfaceComposition.hpp>
#include <NGIN/UI/ControlPrimitives.hpp>
#include <NGIN/UI/Controls.hpp>
#include <NGIN/UI/Surface.hpp>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace NGIN::UI;

TEST_CASE("embedded surfaces retain immutable frames and skip idle stages") {
  Surface::Frame retained;
  InvalidationScheduler invalidate;
  {
    Surface surface;
    invalidate = surface.Invalidator();
    NGIN::UI::Detail::SurfaceComposition::SetContent(surface, [](Composer &composer) {
      NodeProperties properties{};
      properties.paintsBackground = true;
      properties.background = {1, 0, 0, 1};
      composer.Border([] {}, properties, "background");
    });

    auto first = surface.Update({200, 120}, 1);
    REQUIRE(first);
    retained = *first;
    REQUIRE(retained->loadAction == RenderLoadAction::Preserve);
    REQUIRE_FALSE(retained->indices.empty());

    auto idle = surface.Update({200, 120}, 1, MonotonicTime{10});
    REQUIRE(idle);
    CHECK(*idle == retained);
    CHECK(surface.Diagnostics().frameCount == 1);

    invalidate(InvalidationKind::Paint);
    auto painted = surface.Update({200, 120}, 1);
    REQUIRE(painted);
    CHECK(*painted != retained);
    CHECK(surface.Diagnostics().layoutCount == 1);
    CHECK(surface.Diagnostics().compositionCount == 1);
    CHECK(surface.Diagnostics().paintCount == 2);

    invalidate(InvalidationKind::Semantics);
    const auto revision = surface.SemanticRevision();
    auto semantics = surface.Update({200, 120}, 1);
    REQUIRE(semantics);
    CHECK(*semantics == *painted);
    CHECK(surface.SemanticRevision() == revision + 1);

    auto resized = surface.Update({400, 240}, 2);
    REQUIRE(resized);
    CHECK(resized.value()->scaleFactor == 2);
    CHECK(surface.Diagnostics().layoutCount == 2);
    CHECK(surface.Diagnostics().compositionCount == 1);
    CHECK(surface.Tree().Get(surface.Tree().Root())->arrangedBounds.width ==
          200);
  }
  invalidate(InvalidationKind::Compose);
  CHECK(retained->View().targetSize == (PixelSize{200, 120}));
  CHECK_FALSE(retained->View().vertices.empty());
}

TEST_CASE("an embedding host owns focus handoff and pointer capture") {
  Surface surface;
  bool invoked{};
  NGIN::UI::Detail::SurfaceComposition::SetContent(surface, [&](Composer &composer) {
    composer.Button([&] { invoked = true; }, {}, "action");
  });
  REQUIRE(surface.Update({120, 40}, 1));
  auto press = PointerButtonChanged{.pointerId = 1,
                                    .button = PointerButton::Primary,
                                    .state = ButtonState::Pressed,
                                    .position = {5, 5}};
  static_cast<void>(surface.Route(press));
  REQUIRE(surface.CapturedElement(1));
  REQUIRE(surface.FocusedElement());
  static_cast<void>(surface.Route(WindowFocusChanged{.focused = false}));
  CHECK_FALSE(surface.CapturedElement(1));
  CHECK_FALSE(surface.FocusedElement());
  press.state = ButtonState::Released;
  static_cast<void>(surface.Route(press));
  CHECK_FALSE(invoked);
}

TEST_CASE("surface validates viewport and retries failed compositions") {
  Surface surface;
  REQUIRE_FALSE(
      surface.Update({100, 100}, std::numeric_limits<float>::infinity()));
  REQUIRE_FALSE(surface.Update({}, 1));
  int attempts{};
  NGIN::UI::Detail::SurfaceComposition::SetContent(surface, [&](Composer &) {
    if (++attempts == 1)
      throw 1;
  });
  CHECK_FALSE(surface.Update({100, 100}, 1));
  CHECK(surface.IsDirty());
  REQUIRE(surface.Update({100, 100}, 1));
  CHECK(attempts == 2);
}

TEST_CASE(
    "slider input preserves layout and paint of an unrelated mounted panel") {
  Surface surface;
  State<NGIN::F32> volume{0.25F, surface.Invalidator(),
                          InvalidationKind::Compose};
  NodeProperties backdrop;
  backdrop.paintsBackground = true;
  backdrop.layout.preferredSize = {120, 30};
  auto staticPanel = std::make_shared<const ElementDeclaration>(
      ElementType::Border, "unrelated", backdrop);
  NGIN::UI::Detail::SurfaceComposition::SetContent(surface, [&](Composer &composer) {
    composer.Column([&] {
      Slider(composer, Bind(volume), {.minimum = 0, .maximum = 1});
      composer.AppendCached(staticPanel);
    });
  });
  REQUIRE(surface.Update({320, 80}, 1));
  const auto &nodes = surface.Semantics().Nodes();
  auto slider = std::find_if(nodes.begin(), nodes.end(), [](const auto &node) {
    return node.role == SemanticRole::Slider;
  });
  REQUIRE(slider != nodes.end());
  const auto sliderId = slider->id;
  const auto pressAt = Point{slider->bounds.x + slider->bounds.width * 0.8F,
                             slider->bounds.y + slider->bounds.height * 0.5F};
  SECTION("accessibility action") {
    REQUIRE(
        surface.PerformSemanticAction({.node = sliderId,
                                       .action = SemanticActionKind::SetValue,
                                       .numericValue = 0.8}));
  }
  SECTION("pointer press") {
    auto result =
        surface.Route(PointerButtonChanged{.pointerId = 1,
                                           .button = PointerButton::Primary,
                                           .state = ButtonState::Pressed,
                                           .position = pressAt});
    CHECK(result.handled);
  }
  REQUIRE(surface.Update({320, 80}, 1));
  CHECK(volume.Get() > 0.7F);
  CHECK(surface.Diagnostics().layout.reusedMeasurements > 0);
  CHECK(surface.Diagnostics().reusedPaintSubtrees == 1);
}

TEST_CASE("prepared frames pin the glyphs sampled by queued draws") {
  std::weak_ptr<const GlyphAtlasLease> atlasLease;
  PreparedRenderPacket packet;
  {
    auto glyph = std::make_shared<const GlyphAtlasLease>();
    atlasLease = glyph;
    DisplayList list{DrawGlyphRun{
        .atlas = TextureHandle{0, 1},
        .glyphs = {GlyphQuad{.destination = {0, 0, 5, 5}, .lease = glyph}}}};
    packet = UIRenderer{}.Build(list, {20, 20}, 1);
  }
  CHECK_FALSE(atlasLease.expired());
  packet = {};
  CHECK(atlasLease.expired());
}

TEST_CASE("replacing content from its own button releases the old tree after "
          "dispatch") {
  Surface surface;
  std::weak_ptr<int> oldModel;
  {
    auto model = std::make_shared<int>(7);
    oldModel = model;
    NGIN::UI::Detail::SurfaceComposition::SetContent(surface, [&, model](Composer &composer) {
      composer.Button(
          [&, model] {
            NGIN::UI::Detail::SurfaceComposition::SetContent(surface, {});
            CHECK(*model == 7);
          },
          {}, "close");
    });
  }
  REQUIRE(surface.Update({120, 40}, 1));
  REQUIRE(surface.Tree().LiveCount() == 2);
  const auto &nodes = surface.Semantics().Nodes();
  auto button = std::find_if(nodes.begin(), nodes.end(), [](const auto &node) {
    return node.role == SemanticRole::Button;
  });
  REQUIRE(button != nodes.end());
  const auto oldId = button->id;
  REQUIRE(surface.PerformSemanticAction(
      {.node = oldId, .action = SemanticActionKind::Activate}));
  CHECK_FALSE(surface.Semantics().Find(oldId));
  CHECK(surface.Diagnostics().semanticNodeCount == 0);
  CHECK_FALSE(surface.PerformSemanticAction(
      {.node = oldId, .action = SemanticActionKind::Activate}));
  CHECK(surface.Tree().LiveCount() == 1);
  CHECK(oldModel.expired());
}
