#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/NativeText.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/State.hpp>
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
  CHECK(text->AtlasDiagnostics().entryCount == 2);
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
