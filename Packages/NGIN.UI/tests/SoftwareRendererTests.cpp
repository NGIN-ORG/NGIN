#include <NGIN/UI/Testing/SoftwareRenderBackend.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace {
using NGIN::Byte;
using NGIN::UInt32;

auto LoadP3(const char *path) -> NGIN::UI::Testing::SoftwareSurfaceSnapshot {
  std::ifstream input{path};
  std::string magic;
  NGIN::UInt32 width = 0;
  NGIN::UInt32 height = 0;
  NGIN::UInt32 maximum = 0;
  input >> magic >> width >> height >> maximum;
  REQUIRE(input.good());
  REQUIRE(magic == "P3");
  REQUIRE(maximum == 255);

  std::vector<NGIN::Byte> rgba;
  rgba.reserve(static_cast<NGIN::UIntSize>(width) * height * 4U);
  for (NGIN::UIntSize pixel = 0;
       pixel < static_cast<NGIN::UIntSize>(width) * height; ++pixel) {
    NGIN::UInt32 red = 0;
    NGIN::UInt32 green = 0;
    NGIN::UInt32 blue = 0;
    input >> red >> green >> blue;
    REQUIRE(input.good());
    rgba.push_back(static_cast<NGIN::Byte>(red));
    rgba.push_back(static_cast<NGIN::Byte>(green));
    rgba.push_back(static_cast<NGIN::Byte>(blue));
    rgba.push_back(static_cast<NGIN::Byte>(255));
  }
  return {
      .size = {width, height},
      .rgba = std::move(rgba),
  };
}
} // namespace

TEST_CASE("software renderer matches a tolerant visual baseline") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  SoftwareRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).has_value());
  const auto surface =
      renderer.CreateSurface(PlatformWindowHandle{1, 1}, PixelSize{8, 8});
  REQUIRE(surface.has_value());

  const std::array vertices{
      RenderVertex{2.0F, 2.0F, 0.0F, 0.0F, 0xFF2040C0U},
      RenderVertex{6.0F, 2.0F, 1.0F, 0.0F, 0xFF2040C0U},
      RenderVertex{6.0F, 6.0F, 1.0F, 1.0F, 0xFF2040C0U},
      RenderVertex{2.0F, 6.0F, 0.0F, 1.0F, 0xFF2040C0U},
  };
  const std::array<UInt32, 6> indices{0, 1, 2, 0, 2, 3};
  const std::array batches{
      RenderBatch{
          .scissor = {0, 0, 8, 8},
          .firstIndex = 0,
          .indexCount = 6,
          .blendMode = BlendMode::Opaque,
      },
  };
  REQUIRE(renderer
              .Render(surface.value(),
                      RenderPacket{
                          .vertices = vertices,
                          .indices = indices,
                          .batches = batches,
                          .targetSize = {8, 8},
                          .clearColor = {16.0F / 255.0F, 32.0F / 255.0F,
                                         48.0F / 255.0F, 1.0F},
                      })
              .has_value());

  const auto actual = renderer.Snapshot(surface.value());
  REQUIRE(actual.has_value());
  const auto expected =
      LoadP3(NGIN_UI_TEST_SOURCE_DIR "/baselines/software-reference.ppm");
  const auto comparison =
      CompareVisuals(expected, actual.value(),
                     VisualTolerance{.channelDelta = 1,
                                     .maximumDifferentPixelRatio = 0.0,
                                     .maximumMeanAbsoluteError = 0.1});
  CHECK(comparison.dimensionsMatch);
  CHECK(comparison.differentPixelCount == 0);
  CHECK(comparison.maximumChannelDelta == 0);
  CHECK(comparison.passed);

  auto tolerated = expected;
  tolerated.rgba[0] = static_cast<Byte>(17);
  CHECK(CompareVisuals(expected, tolerated,
                       VisualTolerance{.channelDelta = 1,
                                       .maximumDifferentPixelRatio = 0.0,
                                       .maximumMeanAbsoluteError = 0.1})
            .passed);

  tolerated.rgba[0] = static_cast<Byte>(64);
  const auto regression =
      CompareVisuals(expected, tolerated,
                     VisualTolerance{.channelDelta = 1,
                                     .maximumDifferentPixelRatio = 0.0,
                                     .maximumMeanAbsoluteError = 0.1});
  CHECK_FALSE(regression.passed);
  CHECK(regression.differentPixelCount == 1);
}

TEST_CASE("software renderer samples textures and clips batches") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  SoftwareRenderBackend renderer;
  REQUIRE(renderer.Initialize({.enableValidation = true}).has_value());
  const auto surface =
      renderer.CreateSurface(PlatformWindowHandle{2, 1}, PixelSize{4, 4});
  REQUIRE(surface.has_value());
  const auto texture = renderer.CreateTexture(
      TextureCreateInfo{.size = {1, 1}, .format = TextureFormat::RGBA8});
  REQUIRE(texture.has_value());
  const std::array<Byte, 4> blue{static_cast<Byte>(0), static_cast<Byte>(0),
                                 static_cast<Byte>(255),
                                 static_cast<Byte>(255)};
  REQUIRE(renderer
              .UpdateTexture(texture.value(),
                             TextureUpdateInfo{.region = {0, 0, 1, 1},
                                               .bytesPerRow = 4,
                                               .bytes = blue})
              .has_value());

  const std::array vertices{
      RenderVertex{0.0F, 0.0F, 0.0F, 0.0F, 0xFFFFFFFFU},
      RenderVertex{4.0F, 0.0F, 1.0F, 0.0F, 0xFFFFFFFFU},
      RenderVertex{4.0F, 4.0F, 1.0F, 1.0F, 0xFFFFFFFFU},
      RenderVertex{0.0F, 4.0F, 0.0F, 1.0F, 0xFFFFFFFFU},
  };
  const std::array<UInt32, 6> indices{0, 1, 2, 0, 2, 3};
  const std::array batches{
      RenderBatch{
          .texture = texture.value(),
          .scissor = {1, 1, 2, 2},
          .indexCount = 6,
          .blendMode = BlendMode::Opaque,
      },
  };
  REQUIRE(renderer
              .Render(surface.value(),
                      RenderPacket{.vertices = vertices,
                                   .indices = indices,
                                   .batches = batches,
                                   .targetSize = {4, 4},
                                   .clearColor = {0.0F, 0.0F, 0.0F, 1.0F}})
              .has_value());
  const auto snapshot = renderer.Snapshot(surface.value());
  REQUIRE(snapshot.has_value());
  CHECK(snapshot.value().Pixel(0, 0).blue == 0);
  CHECK(snapshot.value().Pixel(1, 1).blue == 255);
  CHECK(snapshot.value().Pixel(2, 2).blue == 255);
  CHECK(snapshot.value().Pixel(3, 3).blue == 0);
  CHECK(renderer.RenderCount() == 1);
  CHECK(renderer.LiveTextureCount() == 1);
}

TEST_CASE("software renderer honors nearest and linear texture filters") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  SoftwareRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).has_value());
  constexpr std::array pixels{
      Byte{0},   Byte{0},   Byte{0},   Byte{255},
      Byte{255}, Byte{255}, Byte{255}, Byte{255},
  };
  constexpr std::array vertices{
      RenderVertex{0.0F, 0.0F, 0.0F, 0.0F, 0xFFFFFFFFU},
      RenderVertex{4.0F, 0.0F, 1.0F, 0.0F, 0xFFFFFFFFU},
      RenderVertex{4.0F, 1.0F, 1.0F, 1.0F, 0xFFFFFFFFU},
      RenderVertex{0.0F, 1.0F, 0.0F, 1.0F, 0xFFFFFFFFU},
  };
  constexpr std::array<UInt32, 6> indices{0, 1, 2, 0, 2, 3};

  UInt32 windowIndex = 20;
  for (const auto filter : {TextureFilter::Nearest, TextureFilter::Linear}) {
    const auto surface = renderer.CreateSurface(
        PlatformWindowHandle{windowIndex++, 1}, PixelSize{4, 1});
    REQUIRE(surface.has_value());
    const auto texture = renderer.CreateTexture(TextureCreateInfo{
        .size = {2, 1},
        .format = TextureFormat::RGBA8,
        .filter = filter,
    });
    REQUIRE(texture.has_value());
    REQUIRE(renderer
                .UpdateTexture(texture.value(),
                               TextureUpdateInfo{
                                   .region = {0, 0, 2, 1},
                                   .bytesPerRow = 8,
                                   .bytes = pixels,
                               })
                .has_value());
    const std::array batches{
        RenderBatch{
            .texture = texture.value(),
            .scissor = {0, 0, 4, 1},
            .indexCount = 6,
            .blendMode = BlendMode::Opaque,
        },
    };
    REQUIRE(renderer
                .Render(surface.value(),
                        RenderPacket{
                            .vertices = vertices,
                            .indices = indices,
                            .batches = batches,
                            .targetSize = {4, 1},
                            .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
                        })
                .has_value());
    const auto snapshot = renderer.Snapshot(surface.value());
    REQUIRE(snapshot.has_value());
    CHECK(snapshot.value().Pixel(0, 0).red == 0);
    CHECK(snapshot.value().Pixel(3, 0).red == 255);
    if (filter == TextureFilter::Nearest) {
      CHECK(snapshot.value().Pixel(1, 0).red == 0);
      CHECK(snapshot.value().Pixel(2, 0).red == 255);
    } else {
      CHECK(snapshot.value().Pixel(1, 0).red >= 63);
      CHECK(snapshot.value().Pixel(1, 0).red <= 65);
      CHECK(snapshot.value().Pixel(2, 0).red >= 190);
      CHECK(snapshot.value().Pixel(2, 0).red <= 192);
    }
  }
}

TEST_CASE("shared shape geometry renders smooth edges at every scale") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  struct ScaleCase final {
    NGIN::F32 scaleFactor;
    PixelSize targetSize;
    UInt32 fillCenter;
    UInt32 strokeCenter;
    UInt32 strokeStart;
  };
  constexpr std::array cases{
      ScaleCase{1.0F, {16, 8}, 4, 12, 8},
      ScaleCase{2.0F, {32, 16}, 8, 24, 16},
  };

  SoftwareRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).has_value());
  UInt32 windowIndex = 10;
  for (const auto &test : cases) {
    const auto surface = renderer.CreateSurface(
        PlatformWindowHandle{windowIndex++, 1}, test.targetSize);
    REQUIRE(surface.has_value());
    const DisplayList displayList{
        FillRoundedRect{
            Rect{1.0F, 1.0F, 6.0F, 6.0F},
            CornerRadius::Uniform(Dp{3.0F}),
            Color{1.0F, 1.0F, 1.0F, 1.0F},
        },
        StrokeRoundedRect{
            Rect{9.0F, 1.0F, 6.0F, 6.0F},
            CornerRadius::Uniform(Dp{3.0F}),
            1.0F,
            Color{1.0F, 1.0F, 1.0F, 1.0F},
        },
    };
    const auto packet =
        UIRenderer{}.Build(displayList, test.targetSize, test.scaleFactor,
                           Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(renderer.Render(surface.value(), packet.View()).has_value());
    const auto snapshot = renderer.Snapshot(surface.value());
    REQUIRE(snapshot.has_value());
    CHECK(snapshot.value().Pixel(test.fillCenter, test.fillCenter).red == 255);
    CHECK(snapshot.value().Pixel(test.strokeCenter, test.fillCenter).red == 0);

    bool strokeHasPartialCoverage = false;
    for (UInt32 y = 0; y < test.targetSize.height; ++y) {
      for (UInt32 x = test.strokeStart; x < test.targetSize.width; ++x) {
        const auto coverage = snapshot.value().Pixel(x, y).red;
        strokeHasPartialCoverage =
            strokeHasPartialCoverage || (coverage > 0 && coverage < 255);
      }
    }
    CHECK(strokeHasPartialCoverage);
  }
}
