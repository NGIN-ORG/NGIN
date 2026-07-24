#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/NativeText.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>

#include <algorithm>
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
  REQUIRE(renderer.TextureUpdates().size() == 1);
  REQUIRE(renderer.TextureUpdates().front().region.width > 0);
  REQUIRE_FALSE(renderer.TextureUpdates().front().bytes.empty());

  auto cached = text->ResolveGlyph(request);
  REQUIRE(cached.HasValue());
  REQUIRE(cached.Value().textureCoordinates ==
          first.Value().textureCoordinates);
  REQUIRE(renderer.TextureUpdates().size() == 1);

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
