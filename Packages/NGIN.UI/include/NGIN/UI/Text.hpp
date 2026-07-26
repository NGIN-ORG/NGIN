#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/TextDirection.hpp>

#include <limits>
#include <memory>
#include <vector>

namespace NGIN::UI {
/// @brief Upright, italic, or oblique request for a font face.
enum class FontSlant : UInt8 {
  Normal,
  Italic,
  Oblique,
};

/// @brief Horizontal alignment of lines within a paragraph width.
enum class TextAlignment : UInt8 {
  Start,
  Center,
  End,
};

/// @brief Whether paragraph layout may wrap at available break opportunities.
enum class TextWrapping : UInt8 {
  NoWrap,
  Wrap,
};

/// @brief Family, size, weight, and slant requested for text shaping.
struct FontRequest final {
  std::vector<NGIN::Text::String> families{};
  UInt16 weight{400};
  UInt16 stretch{100};
  FontSlant slant{FontSlant::Normal};
};

/// @brief Ascender, descender, and line-gap measurements for a font.
struct FontMetrics final {
  F32 ascender{0.0F};
  F32 descender{0.0F};
  F32 lineGap{0.0F};
  F32 unitsPerEm{1.0F};
};

/// @brief UTF-8 text plus font and direction attributes for shaping.
struct TextRun final {
  NGIN::Text::String text{};
  FontRequest font{};
  F32 fontSize{14.0F};
  TextDirection direction{TextDirection::Automatic};
  NGIN::Text::String language{};
  NGIN::Text::String script{};
};

/// @brief Byte range containing one user-perceived text character.
struct GraphemeCluster final {
  UIntSize byteOffset{0};
  UIntSize byteLength{0};
};

/// @brief Glyph index, cluster, advances, and offsets emitted by shaping.
struct ShapedGlyph final {
  UInt32 glyphIndex{0};
  UIntSize clusterByteOffset{0};
  Point offset{};
  Point advance{};
};

/// @brief Font face, metrics, direction, and glyphs produced for a text run.
struct ShapedRun final {
  FontFaceHandle fontFace{};
  TextDirection direction{TextDirection::LeftToRight};
  FontMetrics metrics{};
  std::vector<ShapedGlyph> glyphs{};
  std::vector<GraphemeCluster> graphemeClusters{};
  Size size{};
};

/// @brief Runs, width, wrapping, alignment, and spacing requested for a paragraph.
struct ParagraphRequest final {
  std::vector<TextRun> runs{};
  F32 maximumWidth{std::numeric_limits<F32>::infinity()};
  F32 lineHeight{0.0F};
  TextAlignment alignment{TextAlignment::Start};
  TextWrapping wrapping{TextWrapping::Wrap};
};

/// @brief Shaped run positioned at an origin inside a paragraph.
struct PositionedShapedRun final {
  ShapedRun run{};
  Point origin{};
  F32 fontSize{14.0F};
  UIntSize lineIndex{0};
  UIntSize byteOffset{0};
};

/// @brief Run range and metrics for one laid-out paragraph line.
struct ParagraphLine final {
  UIntSize firstRun{0};
  UIntSize runCount{0};
  Rect bounds{};
  F32 baseline{0.0F};
  UIntSize byteOffset{0};
  UIntSize byteLength{0};
};

/// @brief Positioned runs, lines, and extent produced by paragraph layout.
struct ParagraphLayout final {
  Size size{};
  std::vector<PositionedShapedRun> runs{};
  std::vector<ParagraphLine> lines{};
  UIntSize byteLength{0};
};

/// @brief Font face, glyph index, and pixel size identifying an atlas entry.
struct GlyphAtlasRequest final {
  FontFaceHandle fontFace{};
  UInt32 glyphIndex{0};
  F32 fontSize{14.0F};
  F32 scaleFactor{1.0F};
};

/// @brief Keeps one glyph-atlas page alive while its texture is referenced.
struct GlyphAtlasLease final {};

/// @brief Texture placement, lifetime, and glyph metrics resolved from an atlas.
struct GlyphAtlasEntry final {
  TextureHandle texture{};
  Rect textureCoordinates{};
  Size size{};
  Point bearing{};
  std::shared_ptr<const GlyphAtlasLease> lease{};
};

/// @brief Positioned textured quad and atlas-page lifetime used for one glyph.
struct GlyphQuad final {
  Rect destination{};
  Rect textureCoordinates{};
  std::shared_ptr<const GlyphAtlasLease> lease{};
};

/// @brief Resolves requested fonts and reports their metrics.
class IFontProvider {
public:
  virtual ~IFontProvider() = default;

  [[nodiscard]] virtual auto ResolveFont(const FontRequest &request) noexcept
      -> UIResult<FontFaceHandle> = 0;
  [[nodiscard]] virtual auto Metrics(FontFaceHandle face, F32 fontSize) noexcept
      -> UIResult<FontMetrics> = 0;
};

/// @brief Converts attributed Unicode text runs into positioned glyph sequences.
class ITextShaper {
public:
  virtual ~ITextShaper() = default;

  [[nodiscard]] virtual auto Shape(const TextRun &run,
                                   FontFaceHandle face) noexcept
      -> UIResult<ShapedRun> = 0;
};

/// @brief Wraps and aligns shaped text into paragraph lines.
class ITextLayout {
public:
  virtual ~ITextLayout() = default;

  [[nodiscard]] virtual auto
  LayoutParagraph(const ParagraphRequest &request) noexcept
      -> UIResult<ParagraphLayout> = 0;
};

/// @brief Converts paragraph layout into renderable glyph quads.
class ITextGeometry {
public:
  virtual ~ITextGeometry() = default;

  [[nodiscard]] virtual auto CaretRect(const ParagraphLayout &paragraph,
                                       UIntSize byteOffset) noexcept
      -> UIResult<Rect> = 0;
  [[nodiscard]] virtual auto RangeRects(const ParagraphLayout &paragraph,
                                        UIntSize byteOffset,
                                        UIntSize byteLength) noexcept
      -> UIResult<std::vector<Rect>> = 0;
};

/// @brief Segments UTF-8 text into editing-safe grapheme clusters.
class IGraphemeSegmenter {
public:
  virtual ~IGraphemeSegmenter() = default;

  [[nodiscard]] virtual auto Segment(const NGIN::Text::String &text) noexcept
      -> UIResult<std::vector<GraphemeCluster>> = 0;
};

/// @brief Caches rasterized glyphs in renderer textures.
class IGlyphAtlas {
public:
  virtual ~IGlyphAtlas() = default;

  [[nodiscard]] virtual auto
  ResolveGlyph(const GlyphAtlasRequest &request) noexcept
      -> UIResult<GlyphAtlasEntry> = 0;
};
} // namespace NGIN::UI
