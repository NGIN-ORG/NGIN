#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Image.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
[[nodiscard]] auto SolidPixels(const NGIN::UI::PixelSize size)
    -> NGIN::UI::ImagePixels {
  return NGIN::UI::ImagePixels{
      .size = size,
      .rgba = std::vector<NGIN::Byte>(static_cast<NGIN::UIntSize>(size.width) *
                                          size.height * 4U,
                                      NGIN::Byte{255}),
  };
}

[[nodiscard]] auto PortablePixmap() -> std::vector<NGIN::Byte> {
  constexpr std::string_view value{"P3\n2 1\n255\n255 0 0 0 255 0\n"};
  return {reinterpret_cast<const NGIN::Byte *>(value.data()),
          reinterpret_cast<const NGIN::Byte *>(value.data() + value.size())};
}

[[nodiscard]] auto DecodeBase64(const std::string_view encoded)
    -> std::vector<NGIN::Byte> {
  constexpr std::string_view alphabet{
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
  std::vector<NGIN::Byte> result;
  NGIN::UInt32 accumulator = 0;
  NGIN::UInt32 bitCount = 0;
  for (const auto character : encoded) {
    if (character == '=') {
      break;
    }
    const auto value = alphabet.find(character);
    if (value == std::string_view::npos) {
      continue;
    }
    accumulator = (accumulator << 6U) | static_cast<NGIN::UInt32>(value);
    bitCount += 6U;
    if (bitCount >= 8U) {
      bitCount -= 8U;
      result.push_back(
          static_cast<NGIN::Byte>((accumulator >> bitCount) & 0xFFU));
    }
  }
  return result;
}
} // namespace

TEST_CASE("logical image resources validate and retain RGBA pixels") {
  using namespace NGIN::UI;

  auto invalid = ImageResource::FromPixels(ImagePixels{
      .size = PixelSize{2, 2},
      .rgba = std::vector<NGIN::Byte>(3),
  });
  REQUIRE_FALSE(invalid.HasValue());
  REQUIRE(invalid.Error().code == UIErrorCode::InvalidArgument);

  auto resource = ImageResource::FromPixels(SolidPixels(PixelSize{3, 2}));
  REQUIRE(resource.HasValue());
  REQUIRE(resource.Value()->State() == ImageLoadState::Ready);
  REQUIRE(resource.Value()->Size() == PixelSize{3, 2});
  auto pixels = resource.Value()->CopyPixels();
  REQUIRE(pixels.HasValue());
  REQUIRE(pixels.Value().rgba.size() == 24);
}

TEST_CASE("dynamic image revisions update a stable nearest-filter texture") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto resource = ImageResource::FromPixels(SolidPixels(PixelSize{4, 3}),
                                            TextureFilter::Nearest)
                      .Value();
  ImageTextureCache cache{renderer};

  const auto first = cache.Resolve(resource);
  REQUIRE(first.HasValue());
  REQUIRE(renderer.Textures().size() == 1);
  CHECK(renderer.Textures().front().info.filter == TextureFilter::Nearest);
  const auto revision = resource->Revision();

  auto next = SolidPixels(PixelSize{4, 3});
  next.rgba[0] = NGIN::Byte{17};
  REQUIRE(resource->UpdatePixels(std::move(next)).HasValue());
  CHECK(resource->Revision() == revision + 1);
  const auto updated = cache.Resolve(resource);
  REQUIRE(updated.HasValue());
  CHECK(updated.Value().texture == first.Value().texture);
  CHECK(renderer.Textures().size() == 1);
  CHECK(renderer.TextureUpdates().size() == 2);
  CHECK(std::to_integer<NGIN::UInt8>(
            renderer.TextureUpdates().back().bytes.front()) == 17U);
  CHECK(cache.Diagnostics().uploadCount == 2);
}

TEST_CASE("memory and file image sources decode asynchronously") {
  using namespace NGIN::UI;

  auto memory = ImageResource::DecodeMemoryAsync(
      ImageMemorySource{.encoded = PortablePixmap()});
  memory->Wait();
  REQUIRE(memory->State() == ImageLoadState::Ready);
  REQUIRE(memory->Size() == PixelSize{2, 1});
  const auto memoryPixels = memory->CopyPixels().Value();
  REQUIRE(std::to_integer<NGIN::UInt8>(memoryPixels.rgba[0]) == 255);
  REQUIRE(std::to_integer<NGIN::UInt8>(memoryPixels.rgba[5]) == 255);

  const auto path =
      std::filesystem::temp_directory_path() / "ngin-ui-image-test.ppm";
  {
    const auto bytes = PortablePixmap();
    std::ofstream stream{path, std::ios::binary};
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  auto file = ImageResource::DecodeFileAsync(
      ImageFileSource{.path = NGIN::Text::String{path.string()}});
  file->Wait();
  std::filesystem::remove(path);
  REQUIRE(file->State() == ImageLoadState::Ready);
  REQUIRE(file->Size() == PixelSize{2, 1});

  constexpr std::string_view binaryHeader{"P6\n1 1\n255\n"};
  std::vector<NGIN::Byte> binary{
      reinterpret_cast<const NGIN::Byte *>(binaryHeader.data()),
      reinterpret_cast<const NGIN::Byte *>(binaryHeader.data() +
                                           binaryHeader.size())};
  binary.push_back(NGIN::Byte{10});
  binary.push_back(NGIN::Byte{20});
  binary.push_back(NGIN::Byte{30});
  auto binaryResource = ImageResource::DecodeMemoryAsync(
      ImageMemorySource{.encoded = std::move(binary)});
  binaryResource->Wait();
  REQUIRE(binaryResource->State() == ImageLoadState::Ready);
  const auto binaryPixels = binaryResource->CopyPixels().Value();
  REQUIRE(std::to_integer<NGIN::UInt8>(binaryPixels.rgba[0]) == 10);
  REQUIRE(std::to_integer<NGIN::UInt8>(binaryPixels.rgba[1]) == 20);
  REQUIRE(std::to_integer<NGIN::UInt8>(binaryPixels.rgba[2]) == 30);
}

TEST_CASE("standard image decoder loads PNG and JPEG as RGBA8") {
  using namespace NGIN::UI;

  constexpr std::string_view png{
      "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAEUlEQVR4nGP4z8DQwP"
      "Cf4T8ADn0Dfur2k8AAAAAASUVORK5CYII="};
  auto pngResource = ImageResource::DecodeMemoryAsync(
      ImageMemorySource{.encoded = DecodeBase64(png)});
  pngResource->Wait();
  REQUIRE(pngResource->State() == ImageLoadState::Ready);
  REQUIRE(pngResource->Size() == PixelSize{2, 1});
  const auto pngPixels = pngResource->CopyPixels().Value();
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[0]) == 255);
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[1]) == 0);
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[2]) == 0);
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[3]) == 128);
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[4]) == 0);
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[5]) == 255);
  CHECK(std::to_integer<NGIN::UInt8>(pngPixels.rgba[7]) == 255);

  constexpr std::string_view jpeg{
      "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB"
      "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQH/2wBDAQ"
      "EBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB"
      "AQEBAQEBAQEBAQEBAQEBAQH/wAARCAABAAEDAREAAhEBAxEB/8QAHwAAAQUBAQ"
      "EBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQID"
      "AAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0"
      "NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJip"
      "KTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi"
      "4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBg"
      "cICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFE"
      "KRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RV"
      "VldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqK"
      "mqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6"
      "/9oADAMBAAIRAxEAPwD43r/oQP8AD8//2Q=="};
  auto jpegResource = ImageResource::DecodeMemoryAsync(
      ImageMemorySource{.encoded = DecodeBase64(jpeg)});
  jpegResource->Wait();
  REQUIRE(jpegResource->State() == ImageLoadState::Ready);
  REQUIRE(jpegResource->Size() == PixelSize{1, 1});
  const auto jpegPixels = jpegResource->CopyPixels().Value();
  CHECK(std::abs(static_cast<int>(
                     std::to_integer<NGIN::UInt8>(jpegPixels.rgba[0])) -
                 24) <= 3);
  CHECK(std::abs(static_cast<int>(
                     std::to_integer<NGIN::UInt8>(jpegPixels.rgba[1])) -
                 120) <= 3);
  CHECK(std::abs(static_cast<int>(
                     std::to_integer<NGIN::UInt8>(jpegPixels.rgba[2])) -
                 220) <= 3);
  CHECK(std::to_integer<NGIN::UInt8>(jpegPixels.rgba[3]) == 255);
}

TEST_CASE("standard image decoder rejects malformed and cancelled work") {
  using namespace NGIN::UI;

  StandardImageDecoder decoder;
  std::atomic_bool active{false};
  const std::array malformed{
      NGIN::Byte{0x89}, NGIN::Byte{0x50}, NGIN::Byte{0x4E}, NGIN::Byte{0x47},
      NGIN::Byte{0x0D}, NGIN::Byte{0x0A}, NGIN::Byte{0x1A}, NGIN::Byte{0x0A},
  };
  auto failed = decoder.Decode(malformed, active);
  REQUIRE_FALSE(failed.HasValue());
  CHECK(failed.Error().code == UIErrorCode::ResourceFailed);

  constexpr std::string_view unknown{"not an image"};
  const auto unknownBytes =
      std::span{reinterpret_cast<const NGIN::Byte *>(unknown.data()),
                unknown.size()};
  auto unsupported = decoder.Decode(unknownBytes, active);
  REQUIRE_FALSE(unsupported.HasValue());
  CHECK(unsupported.Error().code == UIErrorCode::Unsupported);

  std::atomic_bool cancelled{true};
  auto cancelledResult = decoder.Decode(malformed, cancelled);
  REQUIRE_FALSE(cancelledResult.HasValue());
  CHECK(cancelledResult.Error().code == UIErrorCode::InvalidState);
}

TEST_CASE("generated image work observes cancellation") {
  using namespace NGIN::UI;

  auto resource = ImageResource::GenerateAsync(ImageGeneratedSource{
      .size = PixelSize{256, 256},
      .pixel =
          [](const NGIN::UInt32 x, const NGIN::UInt32 y) {
            if (x == 0 && y == 0) {
              std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
            return Color{static_cast<NGIN::F32>(x) / 255.0F,
                         static_cast<NGIN::F32>(y) / 255.0F, 0.4F, 1.0F};
          },
  });
  resource->Cancel();
  resource->Wait();
  REQUIRE(resource->State() == ImageLoadState::Cancelled);
  REQUIRE_FALSE(resource->CopyPixels().HasValue());
}

TEST_CASE("generated image callback failures become resource errors") {
  using namespace NGIN::UI;

  auto resource = ImageResource::GenerateAsync(ImageGeneratedSource{
      .size = PixelSize{1, 1},
      .pixel = [](const NGIN::UInt32, const NGIN::UInt32) -> Color {
        throw std::runtime_error{"injected"};
      },
  });
  resource->Wait();
  REQUIRE(resource->State() == ImageLoadState::Failed);
  REQUIRE(resource->Error().code == UIErrorCode::ResourceFailed);
}

TEST_CASE("image texture cache uploads lazily and recreates device resources") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto resource =
      ImageResource::FromPixels(SolidPixels(PixelSize{4, 3})).Value();
  ImageTextureCache cache{renderer};

  auto first = cache.Resolve(resource);
  REQUIRE(first.HasValue());
  REQUIRE(first.Value().state == ImageLoadState::Ready);
  REQUIRE(first.Value().texture);
  REQUIRE(renderer.TextureUpdates().size() == 1);
  CHECK(cache.Diagnostics().missCount == 1);
  CHECK(cache.Diagnostics().uploadCount == 1);
  CHECK(cache.Diagnostics().entryCount == 1);
  CHECK(cache.Diagnostics().maximumEntryCount == 128);
  CHECK(cache.Diagnostics().residentBytes == 48);
  CHECK(cache.Diagnostics().maximumResidentBytes == 256ULL * 1024ULL * 1024ULL);

  auto cached = cache.Resolve(resource);
  REQUIRE(cached.HasValue());
  REQUIRE(cached.Value().texture == first.Value().texture);
  REQUIRE(renderer.TextureUpdates().size() == 1);
  CHECK(cache.Diagnostics().hitCount == 1);

  cache.OnDeviceLost();
  auto unavailable = cache.Resolve(resource);
  REQUIRE_FALSE(unavailable.HasValue());
  CHECK(cache.Diagnostics().evictionCount == 1);
  CHECK(cache.Diagnostics().entryCount == 0);

  Testing::RecordingRenderBackend replacementRenderer;
  REQUIRE(replacementRenderer.Initialize({}).HasValue());
  cache.OnDeviceRestored(replacementRenderer);
  auto recreated = cache.Resolve(resource);
  REQUIRE(recreated.HasValue());
  REQUIRE(recreated.Value().texture);
  REQUIRE(renderer.TextureUpdates().size() == 1);
  REQUIRE(replacementRenderer.TextureUpdates().size() == 1);
  CHECK(cache.Diagnostics().missCount == 2);
  CHECK(cache.Diagnostics().uploadCount == 2);
  CHECK(cache.Diagnostics().entryCount == 1);
}

TEST_CASE("image texture cache enforces entry and memory budgets") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto first = ImageResource::FromPixels(SolidPixels(PixelSize{2, 2})).Value();
  auto second = ImageResource::FromPixels(SolidPixels(PixelSize{2, 2})).Value();
  auto third = ImageResource::FromPixels(SolidPixels(PixelSize{2, 2})).Value();
  ImageTextureCache cache{renderer, ImageTextureCacheOptions{
                                        .maximumEntries = 2,
                                        .maximumResidentBytes = 32,
                                    }};

  REQUIRE(cache.Resolve(first).HasValue());
  REQUIRE(cache.Resolve(second).HasValue());
  REQUIRE(cache.Resolve(first).HasValue());
  REQUIRE(cache.Resolve(third).HasValue());
  const auto bounded = cache.Diagnostics();
  CHECK(bounded.entryCount == 2);
  CHECK(bounded.peakEntryCount == 2);
  CHECK(bounded.residentBytes == 32);
  CHECK(bounded.peakResidentBytes == 32);
  CHECK(bounded.evictionCount == 1);
  CHECK(bounded.hitCount == 1);

  REQUIRE(cache.Resolve(second).HasValue());
  CHECK(cache.Diagnostics().entryCount == 2);
  CHECK(cache.Diagnostics().evictionCount == 2);
  CHECK(cache.Diagnostics().missCount == 4);

  auto tooLarge =
      ImageResource::FromPixels(SolidPixels(PixelSize{3, 3})).Value();
  auto rejected = cache.Resolve(tooLarge);
  REQUIRE_FALSE(rejected.HasValue());
  CHECK(rejected.Error().code == UIErrorCode::ResourceFailed);
  CHECK(cache.Diagnostics().capacityFailureCount == 1);
  CHECK(cache.Diagnostics().entryCount == 2);

  third.reset();
  REQUIRE(cache.Resolve(second).HasValue());
  CHECK(cache.Diagnostics().entryCount == 1);
  CHECK(cache.Diagnostics().evictionCount == 3);
}

TEST_CASE("Image composes fit tint clipping and semantic description") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto resource =
      ImageResource::FromPixels(SolidPixels(PixelSize{200, 100})).Value();
  ImageTextureCache cache{renderer};
  NodeProperties properties{};
  properties.layout.preferredSize = Size{100.0F, 100.0F};
  properties.image.fit = ImageFit::Cover;
  properties.image.alignment = ImageAlignment{0.0F, 0.5F};
  properties.image.tint = Color{0.8F, 0.9F, 1.0F, 0.75F};

  Composer composer;
  composer.Image(resource, cache, NGIN::Text::String{"A generated landscape"},
                 properties, "hero");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto image = tree.Get(tree.Root())->children.front();

  LayoutEngine layout{tree};
  static_cast<void>(
      layout.Measure(image, SizeConstraints{.maximum = Size{100.0F, 100.0F}}));
  layout.Arrange(image, Rect{10.0F, 20.0F, 100.0F, 100.0F});
  const auto *node = tree.Get(image);
  REQUIRE(node->image.valid);
  REQUIRE(node->image.destination == Rect{10.0F, 20.0F, 200.0F, 100.0F});

  const auto display = BuildDisplayList(tree);
  REQUIRE(std::any_of(display.begin(), display.end(),
                      [](const DisplayCommand &command) {
                        return std::holds_alternative<DrawImage>(command);
                      }));
  REQUIRE(std::any_of(display.begin(), display.end(),
                      [](const DisplayCommand &command) {
                        return std::holds_alternative<PushClipRect>(command);
                      }));

  const auto semantics = BuildSemanticTree(tree);
  const auto *semantic = semantics.FindByOwner(node->id);
  REQUIRE(semantic != nullptr);
  REQUIRE(semantic->role == SemanticRole::Image);
  REQUIRE(semantic->description == NGIN::Text::String{"A generated landscape"});
}
