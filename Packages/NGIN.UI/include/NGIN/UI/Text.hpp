#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/TextDirection.hpp>

#include <limits>
#include <vector>

namespace NGIN::UI {
enum class FontSlant : UInt8 {
  Normal,
  Italic,
  Oblique,
};

enum class TextAlignment : UInt8 {
  Start,
  Center,
  End,
};

enum class TextWrapping : UInt8 {
  NoWrap,
  Wrap,
};

struct FontRequest final {
  std::vector<NGIN::Text::String> families{};
  UInt16 weight{400};
  UInt16 stretch{100};
  FontSlant slant{FontSlant::Normal};
};

struct FontMetrics final {
  F32 ascender{0.0F};
  F32 descender{0.0F};
  F32 lineGap{0.0F};
  F32 unitsPerEm{1.0F};
};

struct TextRun final {
  NGIN::Text::String text{};
  FontRequest font{};
  F32 fontSize{14.0F};
  TextDirection direction{TextDirection::Automatic};
  NGIN::Text::String language{};
  NGIN::Text::String script{};
};

struct GraphemeCluster final {
  UIntSize byteOffset{0};
  UIntSize byteLength{0};
};

struct ShapedGlyph final {
  UInt32 glyphIndex{0};
  UIntSize clusterByteOffset{0};
  Point offset{};
  Point advance{};
};

struct ShapedRun final {
  FontFaceHandle fontFace{};
  TextDirection direction{TextDirection::LeftToRight};
  FontMetrics metrics{};
  std::vector<ShapedGlyph> glyphs{};
  std::vector<GraphemeCluster> graphemeClusters{};
  Size size{};
};

struct ParagraphRequest final {
  std::vector<TextRun> runs{};
  F32 maximumWidth{std::numeric_limits<F32>::infinity()};
  F32 lineHeight{0.0F};
  TextAlignment alignment{TextAlignment::Start};
  TextWrapping wrapping{TextWrapping::Wrap};
};

struct PositionedShapedRun final {
  ShapedRun run{};
  Point origin{};
  F32 fontSize{14.0F};
  UIntSize lineIndex{0};
};

struct ParagraphLine final {
  UIntSize firstRun{0};
  UIntSize runCount{0};
  Rect bounds{};
  F32 baseline{0.0F};
};

struct ParagraphLayout final {
  Size size{};
  std::vector<PositionedShapedRun> runs{};
  std::vector<ParagraphLine> lines{};
};

struct GlyphAtlasRequest final {
  FontFaceHandle fontFace{};
  UInt32 glyphIndex{0};
  F32 fontSize{14.0F};
  F32 scaleFactor{1.0F};
};

struct GlyphAtlasEntry final {
  TextureHandle texture{};
  Rect textureCoordinates{};
  Size size{};
  Point bearing{};
};

struct GlyphQuad final {
  Rect destination{};
  Rect textureCoordinates{};
};

class IFontProvider {
public:
  virtual ~IFontProvider() = default;

  [[nodiscard]] virtual auto ResolveFont(const FontRequest &request) noexcept
      -> UIResult<FontFaceHandle> = 0;
  [[nodiscard]] virtual auto Metrics(FontFaceHandle face, F32 fontSize) noexcept
      -> UIResult<FontMetrics> = 0;
};

class ITextShaper {
public:
  virtual ~ITextShaper() = default;

  [[nodiscard]] virtual auto Shape(const TextRun &run,
                                   FontFaceHandle face) noexcept
      -> UIResult<ShapedRun> = 0;
};

class ITextLayout {
public:
  virtual ~ITextLayout() = default;

  [[nodiscard]] virtual auto
  LayoutParagraph(const ParagraphRequest &request) noexcept
      -> UIResult<ParagraphLayout> = 0;
};

class IGraphemeSegmenter {
public:
  virtual ~IGraphemeSegmenter() = default;

  [[nodiscard]] virtual auto Segment(const NGIN::Text::String &text) noexcept
      -> UIResult<std::vector<GraphemeCluster>> = 0;
};

class IGlyphAtlas {
public:
  virtual ~IGlyphAtlas() = default;

  [[nodiscard]] virtual auto
  ResolveGlyph(const GlyphAtlasRequest &request) noexcept
      -> UIResult<GlyphAtlasEntry> = 0;
};
} // namespace NGIN::UI
