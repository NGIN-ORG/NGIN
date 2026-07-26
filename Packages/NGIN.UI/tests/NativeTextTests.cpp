#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/NativeText.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/SoftwareRenderBackend.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

namespace {
[[nodiscard]] auto
CreateTextSystem(NGIN::UI::Testing::RecordingRenderBackend &renderer)
    -> std::unique_ptr<NGIN::UI::NativeTextSystem> {
  REQUIRE(renderer.Initialize({}).HasValue());
  auto created = NGIN::UI::NativeTextSystem::Create(
      renderer, NGIN::UI::NativeTextCreateInfo{
                    .atlasSize = NGIN::UI::PixelSize{256, 256},
                });
  REQUIRE(created.HasValue());
  return std::move(created).Value();
}
} // namespace

TEST_CASE("native text segments extended UTF-8 grapheme clusters") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const NGIN::Text::String value{"Ae\xCC\x81"
                                 "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
                                 "\xF0\x9F\x87\xB8\xF0\x9F\x87\xAA"};

  auto segmented = text->Segment(value);
  REQUIRE(segmented.HasValue());
  REQUIRE(segmented.Value().size() == 4);
  REQUIRE(segmented.Value()[1].byteOffset == 1);
  REQUIRE(segmented.Value()[1].byteLength == 3);
  REQUIRE(segmented.Value()[2].byteLength == 11);
  REQUIRE(segmented.Value()[3].byteLength == 8);

  auto malformed = text->Segment(NGIN::Text::String{"\xF0\x28\x8C\x28"});
  REQUIRE_FALSE(malformed.HasValue());
  REQUIRE(malformed.Error().code == UIErrorCode::TextShapingFailed);
}

TEST_CASE("native text shapes and measures through FreeType and HarfBuzz") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  REQUIRE(std::string_view{NativeTextSystem::BundledFontPath()}.ends_with(
      "NotoSans-Variable.ttf"));

  auto face = text->ResolveFont(FontRequest{});
  REQUIRE(face.HasValue());
  auto metrics = text->Metrics(face.Value(), 18.0F);
  REQUIRE(metrics.HasValue());
  REQUIRE(metrics.Value().ascender > 0.0F);
  REQUIRE(metrics.Value().descender >= 0.0F);

  auto shaped = text->Shape(
      TextRun{
          .text = NGIN::Text::String{"office"},
          .fontSize = 18.0F,
      },
      face.Value());
  REQUIRE(shaped.HasValue());
  REQUIRE_FALSE(shaped.Value().glyphs.empty());
  REQUIRE(shaped.Value().glyphs.size() <= 6);
  REQUIRE(shaped.Value().size.width > 0.0F);
  REQUIRE(shaped.Value().direction == TextDirection::LeftToRight);
  REQUIRE(shaped.Value().graphemeClusters.size() == 6);

  auto rightToLeft = text->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text =
                      NGIN::Text::String{"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"},
                  .fontSize = 18.0F,
              },
          },
      .maximumWidth = 200.0F,
      .wrapping = TextWrapping::NoWrap,
  });
  REQUIRE(rightToLeft.HasValue());
  REQUIRE(rightToLeft.Value().runs.front().run.direction ==
          TextDirection::RightToLeft);
  auto rtlStart = text->CaretRect(rightToLeft.Value(), 0);
  auto rtlEnd = text->CaretRect(rightToLeft.Value(), 8);
  REQUIRE(rtlStart.HasValue());
  REQUIRE(rtlEnd.HasValue());
  REQUIRE(rtlStart.Value().x > rtlEnd.Value().x);

  auto paragraph = text->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text = NGIN::Text::String{"office"},
                  .fontSize = 18.0F,
              },
          },
      .maximumWidth = 200.0F,
      .wrapping = TextWrapping::NoWrap,
  });
  REQUIRE(paragraph.HasValue());
  REQUIRE(paragraph.Value().runs.size() == 1);
  REQUIRE(paragraph.Value().lines.size() == 1);
  REQUIRE(paragraph.Value().size.width > 0.0F);
  REQUIRE(paragraph.Value().size.height > 0.0F);

  auto caret = text->CaretRect(paragraph.Value(), 3);
  REQUIRE(caret.HasValue());
  REQUIRE(caret.Value().height > 0.0F);
  auto selection = text->RangeRects(paragraph.Value(), 1, 3);
  REQUIRE(selection.HasValue());
  REQUIRE(selection.Value().size() == 1);
  REQUIRE(selection.Value().front().width > 0.0F);
}

TEST_CASE("bundled fallback fonts cover the gallery language contract") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  auto paragraph = text->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text =
                      NGIN::Text::String{
                          "naïve café · Ελληνικά · Кириллица · "
                          "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A"
                          "\xD8\xA9 · e\xCC\x81 · "
                          "\xE2\x9C\x93 \xE2\x98\x85"},
                  .fontSize = 18.0F,
              },
          },
      .maximumWidth = 800.0F,
  });
  REQUIRE(paragraph.HasValue());
  REQUIRE_FALSE(paragraph.Value().runs.empty());
  for (const auto &run : paragraph.Value().runs) {
    CHECK(std::all_of(run.run.glyphs.begin(), run.run.glyphs.end(),
                      [](const ShapedGlyph &glyph) {
                        return glyph.glyphIndex != 0;
                      }));
  }

  const auto diagnostics = text->CoverageDiagnostics();
  CHECK(diagnostics.missingCodePointCount == 0);
  CHECK(diagnostics.missingCodePoints.empty());
  CHECK(diagnostics.fallbackCodePointCount > 0);
  REQUIRE(diagnostics.faces.size() == 3);
  const auto arabic = std::find_if(
      diagnostics.faces.begin(), diagnostics.faces.end(), [](const auto &face) {
        return face.family.View().find("Arabic") != std::string_view::npos;
      });
  REQUIRE(arabic != diagnostics.faces.end());
  CHECK(arabic->fallback);
  CHECK(arabic->resolvedCodePointCount > 0);
  const auto symbols = std::find_if(
      diagnostics.faces.begin(), diagnostics.faces.end(), [](const auto &face) {
        return face.family.View().find("Symbols") != std::string_view::npos;
      });
  REQUIRE(symbols != diagnostics.faces.end());
  CHECK(symbols->fallback);
  CHECK(symbols->resolvedCodePointCount >= 2);
}

TEST_CASE("font coverage diagnostics report unsupported color emoji") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  auto paragraph = text->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text = NGIN::Text::String{"\xF0\x9F\x98\x80"},
                  .fontSize = 18.0F,
              },
          },
  });
  REQUIRE(paragraph.HasValue());
  const auto diagnostics = text->CoverageDiagnostics();
  REQUIRE(diagnostics.missingCodePointCount == 1);
  REQUIRE(diagnostics.missingCodePoints.size() == 1);
  CHECK(diagnostics.missingCodePoints.front() == 0x1F600U);
}

TEST_CASE("native text rasterizes and caches renderer-backed atlas glyphs") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto face = text->ResolveFont(FontRequest{}).Value();
  const auto shaped =
      text->Shape(TextRun{.text = NGIN::Text::String{"A"}, .fontSize = 20.0F},
                  face)
          .Value();
  REQUIRE(shaped.glyphs.size() == 1);

  const GlyphAtlasRequest request{
      .fontFace = face,
      .glyphIndex = shaped.glyphs.front().glyphIndex,
      .fontSize = 20.0F,
      .scaleFactor = 2.0F,
  };
  auto first = text->ResolveGlyph(request);
  REQUIRE(first.HasValue());
  REQUIRE(first.Value().texture);
  REQUIRE(first.Value().size.width > 0.0F);
  REQUIRE(renderer.Textures().size() == 1);
  CHECK(renderer.Textures().front().info.format == TextureFormat::R8);
  CHECK(renderer.Textures().front().info.filter == TextureFilter::Nearest);
  REQUIRE(renderer.TextureUpdates().size() == 1);
  REQUIRE(renderer.TextureUpdates().front().region.width > 0);
  REQUIRE_FALSE(renderer.TextureUpdates().front().bytes.empty());
  CHECK(text->AtlasDiagnostics().missCount == 1);
  CHECK(text->AtlasDiagnostics().uploadCount == 1);
  CHECK(text->AtlasDiagnostics().entryCount == 1);
  CHECK(text->AtlasDiagnostics().usedPixelArea > 0);
  CHECK(text->AtlasDiagnostics().atlasSize == PixelSize{256, 256});

  auto cached = text->ResolveGlyph(request);
  REQUIRE(cached.HasValue());
  REQUIRE(cached.Value().textureCoordinates ==
          first.Value().textureCoordinates);
  REQUIRE(renderer.TextureUpdates().size() == 1);
  CHECK(text->AtlasDiagnostics().hitCount == 1);

  const auto whitespace =
      text->Shape(TextRun{.text = NGIN::Text::String{" "}, .fontSize = 20.0F},
                  face)
          .Value();
  auto invisible = text->ResolveGlyph(GlyphAtlasRequest{
      .fontFace = face,
      .glyphIndex = whitespace.glyphs.front().glyphIndex,
      .fontSize = 20.0F,
  });
  REQUIRE(invisible.HasValue());
  REQUIRE_FALSE(invisible.Value().texture);
  REQUIRE(invisible.Value().size == Size{});
  CHECK(text->AtlasDiagnostics().missCount == 2);
  CHECK(text->AtlasDiagnostics().entryCount == 1);
}

TEST_CASE("native text recycles bounded atlas pages after glyphs are released") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto created = NativeTextSystem::Create(
      renderer, NativeTextCreateInfo{
                    .atlasSize = PixelSize{32, 32},
                    .maximumAtlasPages = 2,
                });
  REQUIRE(created.HasValue());
  auto text = std::move(created).Value();
  const auto face = text->ResolveFont(FontRequest{}).Value();
  const auto shaped =
      text->Shape(TextRun{.text = NGIN::Text::String{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
                          .fontSize = 16.0F},
                  face)
          .Value();

  for (NGIN::UInt32 round = 0; round < 8; ++round) {
    for (const auto &glyph : shaped.glyphs) {
      auto resolved = text->ResolveGlyph(GlyphAtlasRequest{
          .fontFace = face,
          .glyphIndex = glyph.glyphIndex,
          .fontSize = 12.0F + static_cast<NGIN::F32>(round % 4U) * 3.0F,
          .scaleFactor = 1.0F,
      });
      REQUIRE(resolved.HasValue());
    }
  }

  const auto diagnostics = text->AtlasDiagnostics();
  CHECK(diagnostics.pageCount == 2);
  CHECK(diagnostics.peakPageCount == 2);
  CHECK(diagnostics.maximumPageCount == 2);
  CHECK(diagnostics.pageRecycleCount > 0);
  CHECK(diagnostics.evictionCount > 0);
  CHECK(diagnostics.allocationFailureCount == 0);
  CHECK(renderer.Textures().size() == 2);
  CHECK(std::all_of(renderer.Textures().begin(), renderer.Textures().end(),
                    [](const auto &texture) { return !texture.destroyed; }));
  CHECK(diagnostics.entryCount < shaped.glyphs.size() * 4U);
  CHECK(diagnostics.capacityPixelArea == 2U * 32U * 32U);
  CHECK(diagnostics.usedPixelArea <= diagnostics.capacityPixelArea);
  CHECK(diagnostics.pixelSizeCount <= 4);
  CHECK(diagnostics.pixelSizes.size() == diagnostics.pixelSizeCount);
  CHECK(std::is_sorted(
      diagnostics.pixelSizes.begin(), diagnostics.pixelSizes.end(),
      [](const auto &left, const auto &right) {
        return left.pixelSize < right.pixelSize;
      }));
}

TEST_CASE("native text does not recycle atlas pages used by display data") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto created = NativeTextSystem::Create(
      renderer, NativeTextCreateInfo{
                    .atlasSize = PixelSize{32, 32},
                    .maximumAtlasPages = 1,
                });
  REQUIRE(created.HasValue());
  auto text = std::move(created).Value();
  const auto face = text->ResolveFont(FontRequest{}).Value();
  const auto shaped =
      text->Shape(TextRun{.text = NGIN::Text::String{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
                          .fontSize = 18.0F},
                  face)
          .Value();
  auto held = text->ResolveGlyph(GlyphAtlasRequest{
      .fontFace = face,
      .glyphIndex = shaped.glyphs.front().glyphIndex,
      .fontSize = 18.0F,
  });
  REQUIRE(held.HasValue());
  REQUIRE(held.Value().lease);

  GlyphAtlasRequest failedRequest{};
  bool capacityReached = false;
  for (NGIN::UInt32 pixelSize = 12; pixelSize <= 24 && !capacityReached;
       pixelSize += 3) {
    for (const auto &glyph : shaped.glyphs) {
      const GlyphAtlasRequest request{
          .fontFace = face,
          .glyphIndex = glyph.glyphIndex,
          .fontSize = static_cast<NGIN::F32>(pixelSize),
      };
      auto resolved = text->ResolveGlyph(request);
      if (!resolved) {
        failedRequest = request;
        capacityReached = true;
        break;
      }
    }
  }
  REQUIRE(capacityReached);
  CHECK(text->AtlasDiagnostics().pageRecycleCount == 0);
  CHECK(text->AtlasDiagnostics().allocationFailureCount == 1);

  held.Value().lease.reset();
  auto recovered = text->ResolveGlyph(failedRequest);
  REQUIRE(recovered.HasValue());
  CHECK(text->AtlasDiagnostics().pageRecycleCount == 1);
}

TEST_CASE("native text rebuilds atlas pages after device restoration") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto face = text->ResolveFont(FontRequest{}).Value();
  const auto shaped =
      text->Shape(TextRun{.text = NGIN::Text::String{"device"},
                          .fontSize = 18.0F},
                  face)
          .Value();
  const GlyphAtlasRequest request{
      .fontFace = face,
      .glyphIndex = shaped.glyphs.front().glyphIndex,
      .fontSize = 18.0F,
  };
  const auto first = text->ResolveGlyph(request);
  REQUIRE(first.HasValue());
  const auto oldTexture = first.Value().texture;
  NGIN::UInt32 invalidationCount = 0;
  text->SetResourcesInvalidatedCallback(
      [&invalidationCount] { ++invalidationCount; });

  text->OnDeviceLost();
  CHECK(invalidationCount == 1);
  CHECK(text->AtlasDiagnostics().pageCount == 0);
  const auto unavailable = text->ResolveGlyph(request);
  REQUIRE_FALSE(unavailable.HasValue());
  CHECK(unavailable.Error().code == UIErrorCode::InvalidState);
  const auto oldRecord =
      std::find_if(renderer.Textures().begin(), renderer.Textures().end(),
                   [oldTexture](const auto &texture) {
                     return texture.handle == oldTexture;
                   });
  REQUIRE(oldRecord != renderer.Textures().end());
  CHECK(oldRecord->destroyed);

  REQUIRE(text->OnDeviceRestored(renderer).HasValue());
  CHECK(invalidationCount == 2);
  CHECK(text->AtlasDiagnostics().restorationCount == 1);
  CHECK(text->AtlasDiagnostics().pageCount == 1);
  CHECK(text->AtlasDiagnostics().entryCount == 0);
  const auto restored = text->ResolveGlyph(request);
  REQUIRE(restored.HasValue());
  CHECK(restored.Value().texture != oldTexture);
}

TEST_CASE("native text drives retained Text layout and glyph display lists") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.text.fontSize = 18.0F;

  Composer composer;
  composer.Text(NGIN::Text::String{"NGIN.UI"}, *text, *text, properties);
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto textHandle = tree.Get(tree.Root())->children.front();

  LayoutEngine layout{tree};
  const auto measured =
      layout.Measure(textHandle, SizeConstraints{
                                     .maximum = Size{200.0F, 50.0F},
                                 });
  REQUIRE(measured.width > 0.0F);
  REQUIRE(measured.height > 0.0F);
  layout.Arrange(textHandle, Rect{0.0F, 0.0F, measured.width, measured.height});

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() >= 3);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList.front()));
  REQUIRE(std::holds_alternative<PopClip>(displayList.back()));
  REQUIRE(std::any_of(displayList.begin(), displayList.end(),
                      [](const DisplayCommand &command) {
                        return std::holds_alternative<DrawGlyphRun>(command);
                      }));
  REQUIRE_FALSE(renderer.TextureUpdates().empty());
}

TEST_CASE("native centered text stays pixel aligned at common DPI scales") {
  using namespace NGIN::UI;

  constexpr std::array scales{1.0F, 1.25F, 1.5F, 2.0F};
  for (const auto scale : scales) {
    Testing::RecordingRenderBackend backend;
    auto text = CreateTextSystem(backend);
    NodeProperties properties{};
    properties.layout.preferredSize.width = 200.0F;
    properties.layout.maximumSize.width = 200.0F;
    properties.layout.horizontalAlignment = HorizontalAlignment::Start;
    properties.layout.verticalAlignment = VerticalAlignment::Start;
    properties.text.fontSize = 16.0F;
    properties.text.lineHeight = 24.0F;
    properties.text.alignment = TextAlignment::Center;

    Composer composer;
    composer.Text(NGIN::Text::String{"Centered gyjpq\nworks on every line."},
                  *text, *text, properties);
    RuntimeTree tree;
    Reconciler reconciler{tree};
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    LayoutEngine layout{tree};
    static_cast<void>(layout.Perform(
        SizeConstraints{.maximum = Size{200.0F, 48.0F}},
        Rect{0.0F, 0.0F, 200.0F, 48.0F}, scale));

    const auto target = PixelSize{
        static_cast<NGIN::UInt32>(std::lround(200.0F * scale)),
        static_cast<NGIN::UInt32>(std::lround(48.0F * scale)),
    };
    const auto packet =
        UIRenderer{}.Build(BuildDisplayList(tree), target, scale);
    REQUIRE_FALSE(packet.vertices.empty());
    REQUIRE_FALSE(packet.batches.empty());
    for (const auto &vertex : packet.vertices) {
      CHECK(vertex.x == std::round(vertex.x));
      CHECK(vertex.y == std::round(vertex.y));
      CHECK(vertex.x >= 0.0F);
      CHECK(vertex.y >= 0.0F);
      CHECK(vertex.x <= static_cast<NGIN::F32>(target.width));
      CHECK(vertex.y <= static_cast<NGIN::F32>(target.height));
    }
  }
}

TEST_CASE("native text captures preserve antialiasing descenders and clipping") {
  using namespace NGIN::UI;

  constexpr std::array scales{1.0F, 1.25F, 1.5F, 2.0F};
  for (const auto scale : scales) {
    Testing::SoftwareRenderBackend renderer;
    REQUIRE(renderer.Initialize({}).HasValue());
    auto created = NativeTextSystem::Create(
        renderer, NativeTextCreateInfo{
                      .atlasSize = PixelSize{256, 256},
                      .maximumAtlasPages = 2,
                  });
    REQUIRE(created.HasValue());
    auto text = std::move(created).Value();

    NodeProperties properties{};
    properties.layout.preferredSize = Size{200.0F, 64.0F};
    properties.layout.maximumSize = Size{200.0F, 64.0F};
    properties.layout.horizontalAlignment = HorizontalAlignment::Start;
    properties.layout.verticalAlignment = VerticalAlignment::Start;
    properties.text.fontSize = 16.0F;
    properties.text.lineHeight = 24.0F;
    properties.text.alignment = TextAlignment::Center;
    properties.text.wrapping = TextWrapping::Wrap;
    properties.text.color = Color{1.0F, 1.0F, 1.0F, 1.0F};

    Composer composer;
    composer.Text(NGIN::Text::String{"Centered gyjpq\nwraps without clipping."},
                  *text, *text, properties);
    RuntimeTree tree;
    Reconciler reconciler{tree};
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    LayoutEngine layout{tree};
    static_cast<void>(layout.Perform(
        SizeConstraints{.maximum = Size{200.0F, 64.0F}},
        Rect{0.0F, 0.0F, 200.0F, 64.0F}, scale));

    const PixelSize target{
        static_cast<NGIN::UInt32>(std::lround(200.0F * scale)),
        static_cast<NGIN::UInt32>(std::lround(64.0F * scale)),
    };
    auto surface =
        renderer.CreateSurface(PlatformWindowHandle{1, 1}, target);
    REQUIRE(surface.HasValue());
    const auto displayList = BuildDisplayList(tree);
    const auto packet = UIRenderer{}.Build(
        displayList, target, scale, Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(renderer.Render(surface.Value(), packet.View()).HasValue());
    const auto snapshot = renderer.Snapshot(surface.Value());
    REQUIRE(snapshot.HasValue());

    NGIN::UInt32 minimumY = target.height;
    NGIN::UInt32 maximumY = 0;
    NGIN::UIntSize inkPixels = 0;
    bool hasAntialiasedEdge = false;
    for (NGIN::UInt32 y = 0; y < target.height; ++y) {
      for (NGIN::UInt32 x = 0; x < target.width; ++x) {
        const auto pixel = snapshot.Value().Pixel(x, y);
        if (pixel.red == 0) {
          continue;
        }
        ++inkPixels;
        minimumY = std::min(minimumY, y);
        maximumY = std::max(maximumY, y);
        hasAntialiasedEdge =
            hasAntialiasedEdge || (pixel.red > 0 && pixel.red < 255);
      }
    }
    CHECK(inkPixels > 100);
    CHECK(minimumY > 0);
    CHECK(maximumY + 1U < target.height);
    CHECK(maximumY > static_cast<NGIN::UInt32>(24.0F * scale));
    CHECK(hasAntialiasedEdge);
  }
}

TEST_CASE("native text lays out explicit and Unicode wrap opportunities") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  auto paragraph = text->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text =
                      NGIN::Text::String{
                          "alpha beta\n"
                          "\xE6\xBC\xA2\xE5\xAD\x97\xE4\xBB\xAE\xE5\x90\x8D"},
                  .fontSize = 18.0F,
              },
          },
      .maximumWidth = 58.0F,
      .alignment = TextAlignment::Center,
      .wrapping = TextWrapping::Wrap,
  });
  REQUIRE(paragraph.HasValue());
  REQUIRE(paragraph.Value().lines.size() >= 3);
  REQUIRE(paragraph.Value().lines.front().byteOffset == 0);
  REQUIRE(paragraph.Value().lines[1].byteOffset > 0);
  REQUIRE(paragraph.Value().size.height >
          paragraph.Value().lines.front().bounds.height);
  REQUIRE(std::all_of(paragraph.Value().runs.begin(),
                      paragraph.Value().runs.end(),
                      [](const PositionedShapedRun &run) {
                        return run.run.fontFace.IsValid();
                      }));

  const auto first = text->CaretRect(
      paragraph.Value(), paragraph.Value().lines.front().byteOffset);
  const auto second =
      text->CaretRect(paragraph.Value(), paragraph.Value().lines[1].byteOffset);
  REQUIRE(first.HasValue());
  REQUIRE(second.HasValue());
  REQUIRE(second.Value().y > first.Value().y);

  const auto selection =
      text->RangeRects(paragraph.Value(), 0, paragraph.Value().byteLength);
  REQUIRE(selection.HasValue());
  REQUIRE(selection.Value().size() >= 2);
}

TEST_CASE("native text accepts configured fallback faces") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).HasValue());
  auto created = NativeTextSystem::Create(
      renderer,
      NativeTextCreateInfo{
          .fallbackFontPaths =
              {
                  NGIN::Text::String{NativeTextSystem::BundledFontPath()},
              },
          .atlasSize = PixelSize{256, 256},
      });
  REQUIRE(created.HasValue());
  auto paragraph = created.Value()->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text = NGIN::Text::String{"Fallback shaping"},
                  .fontSize = 16.0F,
              },
          },
      .maximumWidth = 80.0F,
  });
  REQUIRE(paragraph.HasValue());
  REQUIRE_FALSE(paragraph.Value().runs.empty());
}

TEST_CASE("TextArea edits lines navigates vertically and scrolls its caret") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  State<NGIN::Text::String> value{
      NGIN::Text::String{"first line\nsecond line\nthird line\nfourth line"}};
  NodeProperties properties{};
  properties.layout.preferredSize = Size{120.0F, 44.0F};
  properties.layout.padding = Thickness::Uniform(Dp{4.0F});
  properties.text.layout = text.get();
  properties.text.geometry = text.get();
  properties.text.glyphAtlas = text.get();
  properties.text.fontSize = 16.0F;
  properties.text.wrapping = TextWrapping::Wrap;

  Composer composer;
  composer.TextArea(Bind(value), *text, properties, "notes");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto area = tree.Get(tree.Root())->children.front();
  LayoutEngine layout{tree};
  static_cast<void>(
      layout.Measure(area, SizeConstraints{.maximum = Size{120.0F, 44.0F}}));
  layout.Arrange(area, Rect{0.0F, 0.0F, 120.0F, 44.0F});
  auto *node = tree.Get(area);
  REQUIRE(node->text.paragraph.lines.size() >= 4);
  REQUIRE(node->scroll.contentSize.height > node->scroll.viewportSize.height);

  InputRouter input{tree};
  REQUIRE(input.SetFocus(area));
  const auto initialCaret = node->textField.editing->State().caretCluster;
  auto up = input.Route(KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Up),
      .state = KeyState::Pressed,
  });
  REQUIRE(up.handled);
  REQUIRE(node->textField.editing->State().caretCluster < initialCaret);

  auto entered = input.Route(KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Enter),
      .state = KeyState::Pressed,
  });
  REQUIRE(entered.handled);
  REQUIRE(entered.layoutStateChanged);
  REQUIRE(value.Get().View().find('\n') != std::string_view::npos);

  static_cast<void>(
      layout.Measure(area, SizeConstraints{.maximum = Size{120.0F, 44.0F}}));
  layout.Arrange(area, Rect{0.0F, 0.0F, 120.0F, 44.0F});
  REQUIRE(node->scroll.offset.y >= 0.0F);

  const auto semantics = BuildSemanticTree(tree);
  const auto *semantic = semantics.FindByOwner(node->id);
  REQUIRE(semantic != nullptr);
  REQUIRE(semantic->role == SemanticRole::TextBox);
  REQUIRE(semantic->value == value.Get());
}
