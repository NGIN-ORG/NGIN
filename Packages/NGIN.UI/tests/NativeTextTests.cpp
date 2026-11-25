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
  REQUIRE(renderer.Initialize({}).has_value());
  auto created = NGIN::UI::NativeTextSystem::Create(
      renderer, NGIN::UI::NativeTextCreateInfo{
                    .atlasSize = NGIN::UI::PixelSize{256, 256},
                });
  REQUIRE(created.has_value());
  return std::move(created).value();
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
  REQUIRE(segmented.has_value());
  REQUIRE(segmented.value().size() == 4);
  REQUIRE(segmented.value()[1].byteOffset == 1);
  REQUIRE(segmented.value()[1].byteLength == 3);
  REQUIRE(segmented.value()[2].byteLength == 11);
  REQUIRE(segmented.value()[3].byteLength == 8);

  auto malformed = text->Segment(NGIN::Text::String{"\xF0\x28\x8C\x28"});
  REQUIRE_FALSE(malformed.has_value());
  REQUIRE(malformed.error().code == UIErrorCode::TextShapingFailed);
}

TEST_CASE("native text shapes and measures through FreeType and HarfBuzz") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  REQUIRE(std::string_view{NativeTextSystem::BundledFontPath()}.ends_with(
      "NotoSans-Variable.ttf"));

  auto face = text->ResolveFont(FontRequest{});
  REQUIRE(face.has_value());
  auto metrics = text->Metrics(face.value(), 18.0F);
  REQUIRE(metrics.has_value());
  REQUIRE(metrics.value().ascender > 0.0F);
  REQUIRE(metrics.value().descender >= 0.0F);

  auto shaped = text->Shape(
      TextRun{
          .text = NGIN::Text::String{"office"},
          .fontSize = 18.0F,
      },
      face.value());
  REQUIRE(shaped.has_value());
  REQUIRE_FALSE(shaped.value().glyphs.empty());
  REQUIRE(shaped.value().glyphs.size() <= 6);
  REQUIRE(shaped.value().size.width > 0.0F);
  REQUIRE(shaped.value().direction == TextDirection::LeftToRight);
  REQUIRE(shaped.value().graphemeClusters.size() == 6);

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
  REQUIRE(rightToLeft.has_value());
  REQUIRE(rightToLeft.value().runs.front().run.direction ==
          TextDirection::RightToLeft);
  auto rtlStart = text->CaretRect(rightToLeft.value(), 0);
  auto rtlEnd = text->CaretRect(rightToLeft.value(), 8);
  REQUIRE(rtlStart.has_value());
  REQUIRE(rtlEnd.has_value());
  REQUIRE(rtlStart.value().x > rtlEnd.value().x);

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
  REQUIRE(paragraph.has_value());
  REQUIRE(paragraph.value().runs.size() == 1);
  REQUIRE(paragraph.value().lines.size() == 1);
  REQUIRE(paragraph.value().size.width > 0.0F);
  REQUIRE(paragraph.value().size.height > 0.0F);

  auto caret = text->CaretRect(paragraph.value(), 3);
  REQUIRE(caret.has_value());
  REQUIRE(caret.value().height > 0.0F);
  auto selection = text->RangeRects(paragraph.value(), 1, 3);
  REQUIRE(selection.has_value());
  REQUIRE(selection.value().size() == 1);
  REQUIRE(selection.value().front().width > 0.0F);
}

TEST_CASE("variable weights shape and cache distinct native outlines") {
  using namespace NGIN::UI;
  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto regular = text->ResolveFont({});
  const auto semibold = text->ResolveFont({.weight = 600});
  REQUIRE(regular);
  REQUIRE(semibold);
  REQUIRE(*regular != *semibold);
  REQUIRE(text->ResolveFont({.weight = 600}).value() == *semibold);
  const TextRun sample{.text = "MMMM", .fontSize = 40};
  const auto regularRun = text->Shape(sample, *regular);
  const auto semiboldRun = text->Shape(sample, *semibold);
  REQUIRE(regularRun);
  REQUIRE(semiboldRun);
  CHECK(semiboldRun->size.width > regularRun->size.width);
  CHECK(text->Shape(sample, *regular)->size.width == regularRun->size.width);

  const auto glyph = regularRun->glyphs.front().glyphIndex;
  const auto regularGlyph = text->ResolveGlyph(
      {.fontFace = *regular, .glyphIndex = glyph, .fontSize = 40});
  const auto semiboldGlyph = text->ResolveGlyph(
      {.fontFace = *semibold, .glyphIndex = glyph, .fontSize = 40});
  REQUIRE(regularGlyph);
  REQUIRE(semiboldGlyph);
  CHECK(regularGlyph->textureCoordinates != semiboldGlyph->textureCoordinates);
  CHECK(text->AtlasDiagnostics().entryCount == 2);

  const auto mixed = text->LayoutParagraph(
      {.runs = {
           {.text = "Hello مرحبا", .font = {.weight = 600}, .fontSize = 18}}});
  REQUIRE(mixed);
  const auto arabic =
      text->ResolveFont({.families = {"Noto Sans Arabic"}, .weight = 600});
  REQUIRE(arabic);
  REQUIRE(std::ranges::any_of(
      mixed->runs, [&](const auto &run) { return run.run.fontFace == *arabic; }));
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
  REQUIRE(paragraph.has_value());
  REQUIRE_FALSE(paragraph.value().runs.empty());
  for (const auto &run : paragraph.value().runs) {
    CHECK(std::all_of(
        run.run.glyphs.begin(), run.run.glyphs.end(),
        [](const ShapedGlyph &glyph) { return glyph.glyphIndex != 0; }));
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
  REQUIRE(paragraph.has_value());
  const auto diagnostics = text->CoverageDiagnostics();
  REQUIRE(diagnostics.missingCodePointCount == 1);
  REQUIRE(diagnostics.missingCodePoints.size() == 1);
  CHECK(diagnostics.missingCodePoints.front() == 0x1F600U);
}

TEST_CASE("native text rasterizes and caches renderer-backed atlas glyphs") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto face = text->ResolveFont(FontRequest{}).value();
  const auto shaped =
      text->Shape(TextRun{.text = NGIN::Text::String{"A"}, .fontSize = 20.0F},
                  face)
          .value();
  REQUIRE(shaped.glyphs.size() == 1);

  const GlyphAtlasRequest request{
      .fontFace = face,
      .glyphIndex = shaped.glyphs.front().glyphIndex,
      .fontSize = 20.0F,
      .scaleFactor = 2.0F,
  };
  auto first = text->ResolveGlyph(request);
  REQUIRE(first.has_value());
  REQUIRE(first.value().texture);
  REQUIRE(first.value().size.width > 0.0F);
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
  REQUIRE(cached.has_value());
  REQUIRE(cached.value().textureCoordinates ==
          first.value().textureCoordinates);
  REQUIRE(renderer.TextureUpdates().size() == 1);
  CHECK(text->AtlasDiagnostics().hitCount == 1);

  const auto whitespace =
      text->Shape(TextRun{.text = NGIN::Text::String{" "}, .fontSize = 20.0F},
                  face)
          .value();
  auto invisible = text->ResolveGlyph(GlyphAtlasRequest{
      .fontFace = face,
      .glyphIndex = whitespace.glyphs.front().glyphIndex,
      .fontSize = 20.0F,
  });
  REQUIRE(invisible.has_value());
  REQUIRE_FALSE(invisible.value().texture);
  REQUIRE(invisible.value().size == Size{});
  CHECK(text->AtlasDiagnostics().missCount == 2);
  CHECK(text->AtlasDiagnostics().entryCount == 1);
}

TEST_CASE("native glyph cache shares physical masks across scales and nearby "
          "phases") {
  using namespace NGIN::UI;
  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto face = text->ResolveFont({}).value();
  const auto glyph = text->Shape({.text = "T", .fontSize = 20}, face)
                         ->glyphs.front()
                         .glyphIndex;
  const auto at = [&](float size, float scale, float offset = 0) {
    return text
        ->ResolveGlyph({.fontFace = face,
                        .glyphIndex = glyph,
                        .fontSize = size,
                        .scaleFactor = scale,
                        .horizontalPixelOffset = offset})
        .value();
  };

  const auto normal = at(20, 1);
  const auto dense = at(10, 2);
  CHECK(dense.textureCoordinates == normal.textureCoordinates);
  CHECK(dense.size.width * 2 == normal.size.width);
  CHECK(dense.size.height * 2 == normal.size.height);
  CHECK(dense.bearing.x * 2 == normal.bearing.x);
  CHECK(dense.bearing.y * 2 == normal.bearing.y);
  CHECK(renderer.TextureUpdates().size() == 1);

  // Scrolling across one pixel reuses the same four physical masks.
  for (int step = 0; step < 16; ++step)
    static_cast<void>(at(20, 1, step / 16.F));
  const auto nearlyNextPixel = at(20, 1, 15.F / 16);
  CHECK(nearlyNextPixel.textureCoordinates == normal.textureCoordinates);
  CHECK(nearlyNextPixel.bearing.x + 15.F / 16 == normal.bearing.x + 1);
  CHECK(renderer.TextureUpdates().size() == 4);
}

TEST_CASE(
    "native text recycles bounded atlas pages after glyphs are released") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).has_value());
  auto created =
      NativeTextSystem::Create(renderer, NativeTextCreateInfo{
                                             .atlasSize = PixelSize{32, 32},
                                             .maximumAtlasPages = 2,
                                         });
  REQUIRE(created.has_value());
  auto text = std::move(created).value();
  const auto face = text->ResolveFont(FontRequest{}).value();
  const auto shaped =
      text->Shape(
              TextRun{.text = NGIN::Text::String{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
                      .fontSize = 16.0F},
              face)
          .value();

  for (NGIN::UInt32 round = 0; round < 8; ++round) {
    for (const auto &glyph : shaped.glyphs) {
      auto resolved = text->ResolveGlyph(GlyphAtlasRequest{
          .fontFace = face,
          .glyphIndex = glyph.glyphIndex,
          .fontSize = 12.0F + static_cast<NGIN::F32>(round % 4U) * 3.0F,
          .scaleFactor = 1.0F,
      });
      REQUIRE(resolved.has_value());
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
  CHECK(std::is_sorted(diagnostics.pixelSizes.begin(),
                       diagnostics.pixelSizes.end(),
                       [](const auto &left, const auto &right) {
                         return left.pixelSize < right.pixelSize;
                       }));
}

TEST_CASE("native text does not recycle atlas pages used by display data") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).has_value());
  auto created =
      NativeTextSystem::Create(renderer, NativeTextCreateInfo{
                                             .atlasSize = PixelSize{32, 32},
                                             .maximumAtlasPages = 1,
                                         });
  REQUIRE(created.has_value());
  auto text = std::move(created).value();
  const auto face = text->ResolveFont(FontRequest{}).value();
  const auto shaped =
      text->Shape(
              TextRun{.text = NGIN::Text::String{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
                      .fontSize = 18.0F},
              face)
          .value();
  auto held = text->ResolveGlyph(GlyphAtlasRequest{
      .fontFace = face,
      .glyphIndex = shaped.glyphs.front().glyphIndex,
      .fontSize = 18.0F,
  });
  REQUIRE(held.has_value());
  REQUIRE(held.value().lease);

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

  held.value().lease.reset();
  auto recovered = text->ResolveGlyph(failedRequest);
  REQUIRE(recovered.has_value());
  CHECK(text->AtlasDiagnostics().pageRecycleCount == 1);
}

TEST_CASE("native text rebuilds atlas pages after device restoration") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto face = text->ResolveFont(FontRequest{}).value();
  const auto shaped = text->Shape(TextRun{.text = NGIN::Text::String{"device"},
                                          .fontSize = 18.0F},
                                  face)
                          .value();
  const GlyphAtlasRequest request{
      .fontFace = face,
      .glyphIndex = shaped.glyphs.front().glyphIndex,
      .fontSize = 18.0F,
  };
  const auto first = text->ResolveGlyph(request);
  REQUIRE(first.has_value());
  const auto oldTexture = first.value().texture;
  NGIN::UInt32 invalidationCount = 0;
  text->SetResourcesInvalidatedCallback(
      [&invalidationCount] { ++invalidationCount; });

  text->OnDeviceLost();
  CHECK(invalidationCount == 1);
  CHECK(text->AtlasDiagnostics().pageCount == 0);
  const auto unavailable = text->ResolveGlyph(request);
  REQUIRE_FALSE(unavailable.has_value());
  CHECK(unavailable.error().code == UIErrorCode::InvalidState);
  const auto oldRecord =
      std::find_if(renderer.Textures().begin(), renderer.Textures().end(),
                   [oldTexture](const auto &texture) {
                     return texture.handle == oldTexture;
                   });
  REQUIRE(oldRecord != renderer.Textures().end());
  CHECK(oldRecord->destroyed);

  REQUIRE(text->OnDeviceRestored(renderer).has_value());
  CHECK(invalidationCount == 2);
  CHECK(text->AtlasDiagnostics().restorationCount == 1);
  CHECK(text->AtlasDiagnostics().pageCount == 1);
  CHECK(text->AtlasDiagnostics().entryCount == 0);
  const auto restored = text->ResolveGlyph(request);
  REQUIRE(restored.has_value());
  CHECK(restored.value().texture != oldTexture);
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
    static_cast<void>(
        layout.Perform(SizeConstraints{.maximum = Size{200.0F, 48.0F}},
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

TEST_CASE("ink spacing follows shaped positions for small labels") {
  using namespace NGIN::UI;
  constexpr PixelSize atlas{256, 256};
  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);

  // The first horizontal moment includes the partially covered edge pixels.
  // Measuring the bounds alone would hide the spacing error inside each quad.
  const auto inkCenter = [&](float u, float v) {
    const auto x = std::lround(u * atlas.width);
    const auto y = std::lround(v * atlas.height);
    const auto &uploads = renderer.TextureUpdates();
    const auto found =
        std::find_if(uploads.begin(), uploads.end(), [&](const auto &upload) {
          return upload.region.x == x && upload.region.y == y;
        });
    REQUIRE(found != uploads.end());
    double moment = 0, coverage = 0;
    for (NGIN::UInt32 row = 0; row < found->region.height; ++row) {
      for (NGIN::UInt32 column = 0; column < found->region.width; ++column) {
        const auto alpha = static_cast<unsigned>(
            found->bytes[row * found->bytesPerRow + column]);
        coverage += alpha;
        moment += alpha * (column + 0.5);
      }
    }
    REQUIRE(coverage > 0);
    return moment / coverage;
  };

  const auto face = text->ResolveFont({}).value();
  for (const auto word : {"Themes", "status"}) {
    const auto shape =
        text->Shape({.text = word, .fontSize = 14}, face).value();
    for (const float scale : {1.F, 1.25F, 1.5F, 2.F}) {
      for (const float origin : {0.F, 0.5F}) {
        CAPTURE(word, scale, origin);
        Composer composer;
        NodeProperties label{};
        label.text.fontSize = 14;
        composer.Text(word, *text, *text, label);
        RuntimeTree tree;
        Reconciler reconciler{tree};
        static_cast<void>(reconciler.Reconcile(composer.Declarations()));
        LayoutEngine layout{tree};
        static_cast<void>(layout.Perform({.maximum = {136, 30}},
                                         {origin / scale, 0, 136, 30}, scale));
        const auto packet =
            UIRenderer{}.Build(BuildDisplayList(tree), {280, 64}, scale);
        REQUIRE(packet.vertices.size() == 4 * shape.glyphs.size());
        REQUIRE(renderer.Textures().size() == 1);

        // Remove each glyph's own optical center using its unshifted mask;
        // the remaining distance should equal HarfBuzz's actual pen advance.
        double firstOrigin = 0;
        float pen = 0;
        for (std::size_t i = 0; i < shape.glyphs.size(); ++i) {
          const auto &glyph = shape.glyphs[i];
          const auto mask = text->ResolveGlyph({.fontFace = face,
                                                .glyphIndex = glyph.glyphIndex,
                                                .fontSize = 14,
                                                .scaleFactor = scale})
                                .value();
          const auto &vertex = packet.vertices[4 * i];
          const auto paintedCenter = vertex.x + inkCenter(vertex.u, vertex.v);
          const auto intrinsicCenter =
              mask.bearing.x * scale +
              inkCenter(mask.textureCoordinates.x, mask.textureCoordinates.y);
          const auto originError =
              paintedCenter - intrinsicCenter - (pen + glyph.offset.x) * scale;
          if (i == 0)
            firstOrigin = originError;
          CAPTURE(i);
          // Nearest quarter-pixel placement plus grayscale raster precision.
          CHECK(std::abs(originError - firstOrigin) < 0.17);
          pen += glyph.advance.x;
        }
      }
    }
  }
}

TEST_CASE(
    "native text captures preserve antialiasing descenders and clipping") {
  using namespace NGIN::UI;

  constexpr std::array scales{1.0F, 1.25F, 1.5F, 2.0F};
  for (const auto scale : scales) {
    Testing::SoftwareRenderBackend renderer;
    REQUIRE(renderer.Initialize({}).has_value());
    auto created =
        NativeTextSystem::Create(renderer, NativeTextCreateInfo{
                                               .atlasSize = PixelSize{256, 256},
                                               .maximumAtlasPages = 2,
                                           });
    REQUIRE(created.has_value());
    auto text = std::move(created).value();

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
    static_cast<void>(
        layout.Perform(SizeConstraints{.maximum = Size{200.0F, 64.0F}},
                       Rect{0.0F, 0.0F, 200.0F, 64.0F}, scale));

    const PixelSize target{
        static_cast<NGIN::UInt32>(std::lround(200.0F * scale)),
        static_cast<NGIN::UInt32>(std::lround(64.0F * scale)),
    };
    auto surface = renderer.CreateSurface(PlatformWindowHandle{1, 1}, target);
    REQUIRE(surface.has_value());
    const auto displayList = BuildDisplayList(tree);
    const auto packet = UIRenderer{}.Build(displayList, target, scale,
                                           Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(renderer.Render(surface.value(), packet.View()).has_value());
    const auto snapshot = renderer.Snapshot(surface.value());
    REQUIRE(snapshot.has_value());

    NGIN::UInt32 minimumY = target.height;
    NGIN::UInt32 maximumY = 0;
    NGIN::UIntSize inkPixels = 0;
    bool hasAntialiasedEdge = false;
    for (NGIN::UInt32 y = 0; y < target.height; ++y) {
      for (NGIN::UInt32 x = 0; x < target.width; ++x) {
        const auto pixel = snapshot.value().Pixel(x, y);
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
  REQUIRE(paragraph.has_value());
  REQUIRE(paragraph.value().lines.size() >= 3);
  REQUIRE(paragraph.value().lines.front().byteOffset == 0);
  REQUIRE(paragraph.value().lines[1].byteOffset > 0);
  REQUIRE(paragraph.value().size.height >
          paragraph.value().lines.front().bounds.height);
  REQUIRE(std::all_of(paragraph.value().runs.begin(),
                      paragraph.value().runs.end(),
                      [](const PositionedShapedRun &run) {
                        return run.run.fontFace.IsValid();
                      }));

  const auto first = text->CaretRect(
      paragraph.value(), paragraph.value().lines.front().byteOffset);
  const auto second =
      text->CaretRect(paragraph.value(), paragraph.value().lines[1].byteOffset);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(second.value().y > first.value().y);

  const auto selection =
      text->RangeRects(paragraph.value(), 0, paragraph.value().byteLength);
  REQUIRE(selection.has_value());
  REQUIRE(selection.value().size() >= 2);
}

TEST_CASE("native text accepts configured fallback faces") {
  using namespace NGIN::UI;

  Testing::RecordingRenderBackend renderer;
  REQUIRE(renderer.Initialize({}).has_value());
  auto created = NativeTextSystem::Create(
      renderer,
      NativeTextCreateInfo{
          .fallbackFontPaths =
              {
                  NGIN::Text::String{NativeTextSystem::BundledFontPath()},
              },
          .atlasSize = PixelSize{256, 256},
      });
  REQUIRE(created.has_value());
  auto paragraph = created.value()->LayoutParagraph(ParagraphRequest{
      .runs =
          {
              TextRun{
                  .text = NGIN::Text::String{"Fallback shaping"},
                  .fontSize = 16.0F,
              },
          },
      .maximumWidth = 80.0F,
  });
  REQUIRE(paragraph.has_value());
  REQUIRE_FALSE(paragraph.value().runs.empty());
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

TEST_CASE("wrapped editor lines follow the allocated width and wheel history "
          "survives layout") {
  using namespace NGIN::UI;
  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  State<NGIN::Text::String> value{
      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"};
  NodeProperties properties{};
  properties.layout.preferredSize = {220, 42};
  properties.layout.padding = Thickness::Uniform(Dp{4});
  properties.text.layout = text.get();
  properties.text.geometry = text.get();
  properties.text.glyphAtlas = text.get();
  properties.text.fontSize = 16;
  properties.scroll.wheelStep = 10;
  Composer composer;
  composer.TextArea(Bind(value), *text, properties, "notes");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  const auto area = tree.Get(tree.Root())->children.front();
  auto *node = tree.Get(area);
  LayoutEngine layout{tree};
  const auto reflow = [&] {
    static_cast<void>(layout.Perform({.maximum = {220, 42}}, {0, 0, 60, 42}));
  };
  reflow();
  REQUIRE(node->text.paragraph.lines.size() > 4);
  for (const auto &line : node->text.paragraph.lines)
    CHECK(line.bounds.width <= node->scroll.viewportSize.width);
  CHECK(node->scroll.contentSize == node->text.paragraph.size);
  InputRouter input{tree};
  REQUIRE(input.SetFocus(area));
  const auto key = [&](LogicalKey code) {
    REQUIRE(input
                .Route(KeyChanged{.logicalKey = static_cast<NGIN::UInt32>(code),
                                  .state = KeyState::Pressed})
                .layoutStateChanged);
    reflow();
  };
  key(LogicalKey::Home);
  REQUIRE(node->scroll.offset.y > 0);
  const auto previousY = node->text.caretRect.y;
  key(LogicalKey::Up);
  REQUIRE(node->text.caretRect.y < previousY);
  key(LogicalKey::Down);
  REQUIRE(node->text.caretRect.y == previousY);
  const auto beforeWheel = node->scroll.offset.y;
  REQUIRE(input
              .Route(PointerWheelChanged{
                  .pointerId = 1, .delta = {0, 1}, .position = {15, 15}})
              .handled);
  reflow();
  CHECK(node->scroll.offset.y == beforeWheel - 10);
  key(LogicalKey::Right);
  CHECK(node->scroll.offset.y >= beforeWheel);
  CHECK(node->scroll.offset.x == 0);
}

TEST_CASE("fractional font sizes preserve shaped width and raster size") {
  using namespace NGIN::UI;
  Testing::RecordingRenderBackend renderer;
  auto text = CreateTextSystem(renderer);
  const auto face = text->ResolveFont({});
  REQUIRE(face.has_value());

  const auto measure = [&](NGIN::F32 size) {
    auto run = text->Shape(TextRun{.text = "Themes status", .fontSize = size}, *face);
    REQUIRE(run.has_value());
    return run->size.width;
  };
  const auto baseWidth = measure(14.0F);
  // Both requests round to the same whole ppem. The advances retain the
  // requested size to within the 1/64-pixel precision of each shaped glyph.
  for (const auto size : {14.25F, 14.375F}) {
    const auto expectedWidth = baseWidth * size / 14.0F;
    CHECK(std::abs(measure(size) - expectedWidth) < 0.18F);
  }

  const auto run = text->Shape(TextRun{.text = "m", .fontSize = 14.0F}, *face);
  REQUIRE(run.has_value());
  const auto glyph = run->glyphs.front().glyphIndex;
  const auto resolve = [&](NGIN::F32 size, NGIN::F32 scale) {
    return text->ResolveGlyph({.fontFace = *face, .glyphIndex = glyph,
                               .fontSize = size, .scaleFactor = scale});
  };
  const auto first = resolve(14.0F, 1.25F); // 17.5 physical pixels
  const auto second = resolve(18.0F, 1.0F);
  const auto alias = resolve(17.5F, 1.0F);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(alias.has_value());
  CHECK(text->AtlasDiagnostics().uploadCount == 2);
  CHECK(first->textureCoordinates == alias->textureCoordinates);
  CHECK(std::abs(first->size.width * 1.25F - alias->size.width) < 0.001F);
}
