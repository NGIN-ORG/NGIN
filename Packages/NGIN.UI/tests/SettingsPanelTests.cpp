#include "SettingsPanel.hpp"
#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Surface.hpp>
#include <NGIN/UI/Testing/SoftwareRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("settings panel edits and applies a volume in a window and an engine "
          "frame") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;
  auto app = CreateApplication(
      {.platform = std::make_unique<TestPlatformBackend>(),
       .renderer = std::make_unique<SoftwareRenderBackend>()});
  REQUIRE(app);
  auto &renderer = static_cast<SoftwareRenderBackend &>((*app)->Renderer());
  auto text = NativeTextSystem::Create(renderer);
  REQUIRE(text);
  const Views::ViewEnvironment env{
      std::shared_ptr<NativeTextSystem>{std::move(*text)}};
  const PixelSize extent{800, 480};
  auto window = (*app)->CreateWindow({.id = "settings", .initialSize = extent});
  REQUIRE(window);
  Surface overlay;
  bool embedded = false;
  SECTION("standalone window") {}
  SECTION("embedded scene") { embedded = true; }
  auto &surface = embedded ? overlay : (*window)->ContentSurface();
  surface.SetContent(SettingsDemo::SettingsPanel(), env);
  const auto tick = [&] {
    if (embedded) {
      auto frame = overlay.Update(extent, 1);
      REQUIRE(frame);
      REQUIRE(renderer.Render((*window)->SurfaceHandle(), (*frame)->View()));
    } else {
      REQUIRE((*app)->PumpOnce());
    }
  };
  const auto find = [&surface](SemanticRole role,
                               std::string_view label) -> SemanticNode {
    for (const auto &node : surface.Semantics().Nodes()) {
      if (node.role == role && node.label.CStr() == label) {
        return node;
      }
    }
    FAIL("Missing accessible node: " << label);
    return {};
  };
  if (embedded) {
    REQUIRE(renderer.Render(
        (*window)->SurfaceHandle(),
        RenderPacket{.targetSize = extent, .clearColor = {1, 0, 0, 1}}));
  }
  tick();
  const auto slider = find(SemanticRole::Slider, "Master volume");
  REQUIRE(surface.PerformSemanticAction({.node = slider.id,
                                         .action = SemanticActionKind::SetValue,
                                         .numericValue = 0.8}));
  tick();
  CHECK(find(SemanticRole::ProgressBar, "Output level").value ==
        NGIN::Text::String{"80%"});
  const auto button = find(SemanticRole::Button, "Apply");
  REQUIRE(surface.PerformSemanticAction(
      {.node = button.id, .action = SemanticActionKind::Activate}));
  tick();
  CHECK(std::ranges::any_of(
      surface.Semantics().Nodes(), [](const SemanticNode &node) {
        return node.role == SemanticRole::Text &&
               node.value == NGIN::Text::String{"Player at 80%"} &&
               node.bounds.width > 0;
      }));
  if (embedded) {
    auto image = renderer.Snapshot((*window)->SurfaceHandle());
    REQUIRE(image);
    CHECK(image->Pixel(1, 479).red == 255);
    auto previous = overlay.Update(extent, 1);
    REQUIRE(previous);
    auto idle = overlay.Update(extent, 1);
    REQUIRE(idle);
    CHECK(*idle == *previous);
  }
}
