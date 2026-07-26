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
  REQUIRE(renderer.Initialize({}).HasValue());
  const auto surface =
      renderer.CreateSurface(PlatformWindowHandle{1, 1}, PixelSize{8, 8});
  REQUIRE(surface.HasValue());

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
              .Render(surface.Value(),
                      RenderPacket{
                          .vertices = vertices,
                          .indices = indices,
                          .batches = batches,
                          .targetSize = {8, 8},
                          .clearColor = {16.0F / 255.0F, 32.0F / 255.0F,
                                         48.0F / 255.0F, 1.0F},
                      })
              .HasValue());

  const auto actual = renderer.Snapshot(surface.Value());
  REQUIRE(actual.HasValue());
  const auto expected =
      LoadP3(NGIN_UI_TEST_SOURCE_DIR "/baselines/software-reference.ppm");
  const auto comparison =
      CompareVisuals(expected, actual.Value(),
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
  REQUIRE(renderer.Initialize({.enableValidation = true}).HasValue());
  const auto surface =
      renderer.CreateSurface(PlatformWindowHandle{2, 1}, PixelSize{4, 4});
  REQUIRE(surface.HasValue());
  const auto texture = renderer.CreateTexture(
      TextureCreateInfo{.size = {1, 1}, .format = TextureFormat::RGBA8});
  REQUIRE(texture.HasValue());
  const std::array<Byte, 4> blue{static_cast<Byte>(0), static_cast<Byte>(0),
                                 static_cast<Byte>(255),
                                 static_cast<Byte>(255)};
  REQUIRE(renderer
              .UpdateTexture(texture.Value(),
                             TextureUpdateInfo{.region = {0, 0, 1, 1},
                                               .bytesPerRow = 4,
                                               .bytes = blue})
              .HasValue());

  const std::array vertices{
      RenderVertex{0.0F, 0.0F, 0.0F, 0.0F, 0xFFFFFFFFU},
      RenderVertex{4.0F, 0.0F, 1.0F, 0.0F, 0xFFFFFFFFU},
      RenderVertex{4.0F, 4.0F, 1.0F, 1.0F, 0xFFFFFFFFU},
      RenderVertex{0.0F, 4.0F, 0.0F, 1.0F, 0xFFFFFFFFU},
  };
  const std::array<UInt32, 6> indices{0, 1, 2, 0, 2, 3};
  const std::array batches{
      RenderBatch{
          .texture = texture.Value(),
          .scissor = {1, 1, 2, 2},
          .indexCount = 6,
          .blendMode = BlendMode::Opaque,
      },
  };
  REQUIRE(renderer
              .Render(surface.Value(),
                      RenderPacket{.vertices = vertices,
                                   .indices = indices,
                                   .batches = batches,
                                   .targetSize = {4, 4},
                                   .clearColor = {0.0F, 0.0F, 0.0F, 1.0F}})
              .HasValue());
  const auto snapshot = renderer.Snapshot(surface.Value());
  REQUIRE(snapshot.HasValue());
  CHECK(snapshot.Value().Pixel(0, 0).blue == 0);
  CHECK(snapshot.Value().Pixel(1, 1).blue == 255);
  CHECK(snapshot.Value().Pixel(2, 2).blue == 255);
  CHECK(snapshot.Value().Pixel(3, 3).blue == 0);
  CHECK(renderer.RenderCount() == 1);
  CHECK(renderer.LiveTextureCount() == 1);
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
  REQUIRE(renderer.Initialize({}).HasValue());
  UInt32 windowIndex = 10;
  for (const auto &test : cases) {
    const auto surface = renderer.CreateSurface(
        PlatformWindowHandle{windowIndex++, 1}, test.targetSize);
    REQUIRE(surface.HasValue());
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
    REQUIRE(renderer.Render(surface.Value(), packet.View()).HasValue());
    const auto snapshot = renderer.Snapshot(surface.Value());
    REQUIRE(snapshot.HasValue());
    CHECK(snapshot.Value().Pixel(test.fillCenter, test.fillCenter).red == 255);
    CHECK(snapshot.Value().Pixel(test.strokeCenter, test.fillCenter).red == 0);

    bool strokeHasPartialCoverage = false;
    for (UInt32 y = 0; y < test.targetSize.height; ++y) {
      for (UInt32 x = test.strokeStart; x < test.targetSize.width; ++x) {
        const auto coverage = snapshot.Value().Pixel(x, y).red;
        strokeHasPartialCoverage =
            strokeHasPartialCoverage || (coverage > 0 && coverage < 255);
      }
    }
    CHECK(strokeHasPartialCoverage);
  }
}
