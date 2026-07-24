#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
#include <NGIN/UI/Text.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <type_traits>

using namespace std::chrono_literals;

namespace {
class LegacyPlatformBackend final
    : public NGIN::UI::Testing::TestPlatformBackend {
public:
  [[nodiscard]] auto ContractVersion() const noexcept
      -> NGIN::UI::BackendContractVersion override {
    return NGIN::UI::BackendContractVersion{0, 9};
  }
};

class LimitedRenderBackend final
    : public NGIN::UI::Testing::RecordingRenderBackend {
public:
  [[nodiscard]] auto Capabilities() const noexcept
      -> NGIN::UI::RenderCapabilityFlags override {
    return NGIN::UI::RenderCapabilityFlags::TextureUpdates;
  }
};
} // namespace

TEST_CASE(
    "device-independent geometry converts and constrains deterministically") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Units;

  REQUIRE(ToPixels(12.5_dp, 2.0F) == Px{25});
  REQUIRE(ToPixelSize(Size{100.25F, 50.5F}, 2.0F) == PixelSize{201, 101});
  REQUIRE(ToPixelRect(Rect{1.0F, 2.0F, 3.0F, 4.0F}, 2.0F) ==
          PixelRect{2, 4, 6, 8});

  const SizeConstraints constraints{
      .minimum = Size{20.0F, 10.0F},
      .maximum = Size{100.0F, 80.0F},
  };
  REQUIRE(constraints.Constrain(Size{10.0F, 90.0F}) == Size{20.0F, 80.0F});
  REQUIRE(Rect{10.0F, 20.0F, 30.0F, 40.0F}.Contains(Point{20.0F, 30.0F}));
  REQUIRE_FALSE(Rect{10.0F, 20.0F, 30.0F, 40.0F}.Contains(Point{40.0F, 30.0F}));
}

TEST_CASE("generational handles distinguish invalid and stale identities") {
  using NGIN::UI::PlatformWindowHandle;

  const PlatformWindowHandle invalid{};
  const PlatformWindowHandle first{4, 1};
  const PlatformWindowHandle reused{4, 2};

  REQUIRE_FALSE(invalid.IsValid());
  REQUIRE(first.IsValid());
  REQUIRE(first != reused);
}

TEST_CASE("text contracts preserve shaping and grapheme cluster metadata") {
  using namespace NGIN::UI;

  STATIC_REQUIRE(std::is_abstract_v<IFontProvider>);
  STATIC_REQUIRE(std::is_abstract_v<ITextShaper>);
  STATIC_REQUIRE(std::is_abstract_v<ITextLayout>);
  STATIC_REQUIRE(std::is_abstract_v<ITextGeometry>);
  STATIC_REQUIRE(std::is_abstract_v<IGraphemeSegmenter>);
  STATIC_REQUIRE(std::is_abstract_v<IGlyphAtlas>);

  const FontFaceHandle face{3, 1};
  const ShapedRun shaped{
      .fontFace = face,
      .direction = TextDirection::LeftToRight,
      .metrics =
          FontMetrics{
              .ascender = 12.0F,
              .descender = 4.0F,
              .lineGap = 2.0F,
              .unitsPerEm = 1000.0F,
          },
      .glyphs =
          {
              ShapedGlyph{
                  .glyphIndex = 42,
                  .clusterByteOffset = 0,
                  .advance = Point{9.0F, 0.0F},
              },
          },
      .graphemeClusters =
          {
              GraphemeCluster{.byteOffset = 0, .byteLength = 2},
          },
      .size = Size{9.0F, 18.0F},
  };

  REQUIRE(shaped.fontFace == face);
  REQUIRE(shaped.glyphs.front().clusterByteOffset == 0);
  REQUIRE(shaped.graphemeClusters.front().byteLength == 2);
  REQUIRE(shaped.size == Size{9.0F, 18.0F});
}

TEST_CASE("headless platform records logical window services and deterministic "
          "time") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  TestPlatformBackend backend;
  auto beforeInit = backend.CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"BeforeInit"},
      .title = NGIN::Text::String{"Before init"},
  });
  REQUIRE_FALSE(beforeInit.HasValue());
  REQUIRE(beforeInit.Error().code == UIErrorCode::BackendUnavailable);

  REQUIRE(backend.Initialize(PlatformInitInfo{}).HasValue());
  REQUIRE(backend.ContractVersion() == CurrentBackendContractVersion);
  REQUIRE(HasPlatformCapability(backend.Capabilities(),
                                PlatformCapabilityFlags::Clipboard));
  REQUIRE(HasPlatformCapability(backend.Capabilities(),
                                PlatformCapabilityFlags::MultipleWindows));
  auto created = backend.CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Main"},
      .title = NGIN::Text::String{"Main window"},
      .initialSize = PixelSize{800, 600},
  });
  REQUIRE(created.HasValue());
  REQUIRE(backend.ShowWindow(created.Value()).HasValue());
  REQUIRE(backend.SetCursor(created.Value(), CursorShape::Text).HasValue());
  REQUIRE(backend.SetClipboardText(NGIN::Text::String{"clipboard"}).HasValue());
  REQUIRE(backend.StartTextInput(created.Value(), PixelRect{10, 20, 30, 40})
              .HasValue());

  REQUIRE(backend.Windows().size() == 1);
  REQUIRE(backend.Windows().front().visible);
  REQUIRE(backend.Windows().front().cursor == CursorShape::Text);
  REQUIRE(backend.Windows().front().textInputActive);
  REQUIRE(static_cast<bool>(backend.ClipboardText().View() ==
                            std::string_view{"clipboard"}));

  class Sink final : public IPlatformEventSink {
  public:
    void Push(PlatformEvent event) override {
      events.push_back(std::move(event));
    }
    std::vector<PlatformEvent> events{};
  } sink;

  backend.InjectEvent(WindowResized{created.Value(), PixelSize{1024, 768}});
  REQUIRE(backend.WaitEvents(sink, 50ms).HasValue());
  REQUIRE(sink.events.size() == 1);
  REQUIRE(backend.Now() == 0ms);
  REQUIRE(backend.WaitEvents(sink, 50ms).HasValue());
  REQUIRE(backend.Now() == 50ms);
}

TEST_CASE("recording renderer deep-copies packets and texture updates") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  RecordingRenderBackend backend;
  REQUIRE(backend.ContractVersion() == CurrentBackendContractVersion);
  REQUIRE(
      HasRenderCapability(backend.Capabilities(), RequiredRenderCapabilities));
  REQUIRE(
      backend.Initialize(RenderInitInfo{.enableValidation = true}).HasValue());
  auto surface =
      backend.CreateSurface(PlatformWindowHandle{0, 1}, PixelSize{640, 480});
  REQUIRE(surface.HasValue());

  auto texture = backend.CreateTexture(TextureCreateInfo{
      .size = PixelSize{2, 2},
      .format = TextureFormat::RGBA8,
  });
  REQUIRE(texture.HasValue());

  std::array<NGIN::Byte, 4> pixels{
      static_cast<NGIN::Byte>(1),
      static_cast<NGIN::Byte>(2),
      static_cast<NGIN::Byte>(3),
      static_cast<NGIN::Byte>(4),
  };
  const TextureUpdate update{
      .texture = texture.Value(),
      .update =
          TextureUpdateInfo{
              .region = PixelRect{0, 0, 1, 1},
              .bytesPerRow = 4,
              .bytes = pixels,
          },
  };
  const std::array<RenderVertex, 1> vertices{
      RenderVertex{.x = 1.0F, .y = 2.0F},
  };
  const std::array<TextureUpdate, 1> updates{update};
  const RenderPacket packet{
      .vertices = vertices,
      .textureUpdates = updates,
      .targetSize = PixelSize{640, 480},
  };

  REQUIRE(backend.Render(surface.Value(), packet).HasValue());
  pixels[0] = static_cast<NGIN::Byte>(9);

  REQUIRE(backend.RenderPackets().size() == 1);
  REQUIRE(backend.RenderPackets().front().vertices.size() == 1);
  REQUIRE(backend.RenderPackets().front().textureUpdates.size() == 1);
  REQUIRE(std::to_integer<int>(backend.RenderPackets()
                                   .front()
                                   .textureUpdates.front()
                                   .bytes.front()) == 1);
  REQUIRE(backend.ValidationEnabled());
}

TEST_CASE("application rejects incompatible or incomplete backend contracts") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  const BackendContractVersion newerMinor{1, 1};
  const BackendContractVersion legacyVersion{0, 9};
  REQUIRE(newerMinor.Supports(CurrentBackendContractVersion));
  REQUIRE_FALSE(legacyVersion.Supports(CurrentBackendContractVersion));

  auto legacyPlatform = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<LegacyPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE_FALSE(legacyPlatform.HasValue());
  REQUIRE(legacyPlatform.Error().code == UIErrorCode::Unsupported);
  REQUIRE(legacyPlatform.Error().operation ==
          NGIN::Text::String{"ValidateBackendContract"});

  auto limitedRenderer = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<LimitedRenderBackend>(),
  });
  REQUIRE_FALSE(limitedRenderer.HasValue());
  REQUIRE(limitedRenderer.Error().code == UIErrorCode::Unsupported);
  REQUIRE(limitedRenderer.Error().operation ==
          NGIN::Text::String{"ValidateBackendCapabilities"});
}
