#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Rendering.hpp>
#include <NGIN/UI/Text.hpp>

#include <memory>

namespace NGIN::UI {
/// @brief Font search paths, face size, and atlas dimensions for native text.
struct NativeTextCreateInfo final {
  NGIN::Text::String fontPath{};
  std::vector<NGIN::Text::String> fallbackFontPaths{};
  PixelSize atlasSize{1024, 1024};
};

/// @brief Hit, miss, upload, occupancy, and area counters for the glyph atlas.
struct GlyphAtlasDiagnostics final {
  UInt64 hitCount{0};
  UInt64 missCount{0};
  UInt64 uploadCount{0};
  UIntSize entryCount{0};
  UInt64 usedPixelArea{0};
  PixelSize atlasSize{};
};

/// @brief FreeType and HarfBuzz implementation of font, shaping, and glyph services.
class NativeTextSystem final : public IFontProvider,
                               public ITextShaper,
                               public ITextLayout,
                               public ITextGeometry,
                               public IGraphemeSegmenter,
                               public IGlyphAtlas {
public:
  [[nodiscard]] static auto
  Create(IRenderBackend &renderer,
         const NativeTextCreateInfo &info = {}) noexcept
      -> UIResult<std::unique_ptr<NativeTextSystem>>;

  NativeTextSystem(const NativeTextSystem &) = delete;
  NativeTextSystem(NativeTextSystem &&) = delete;
  auto operator=(const NativeTextSystem &) -> NativeTextSystem & = delete;
  auto operator=(NativeTextSystem &&) -> NativeTextSystem & = delete;
  ~NativeTextSystem() override;

  [[nodiscard]] static auto BundledFontPath() noexcept -> const char *;

  [[nodiscard]] auto ResolveFont(const FontRequest &request) noexcept
      -> UIResult<FontFaceHandle> override;
  [[nodiscard]] auto Metrics(FontFaceHandle face, F32 fontSize) noexcept
      -> UIResult<FontMetrics> override;
  [[nodiscard]] auto Shape(const TextRun &run, FontFaceHandle face) noexcept
      -> UIResult<ShapedRun> override;
  [[nodiscard]] auto LayoutParagraph(const ParagraphRequest &request) noexcept
      -> UIResult<ParagraphLayout> override;
  [[nodiscard]] auto CaretRect(const ParagraphLayout &paragraph,
                               UIntSize byteOffset) noexcept
      -> UIResult<Rect> override;
  [[nodiscard]] auto RangeRects(const ParagraphLayout &paragraph,
                                UIntSize byteOffset,
                                UIntSize byteLength) noexcept
      -> UIResult<std::vector<Rect>> override;
  [[nodiscard]] auto Segment(const NGIN::Text::String &text) noexcept
      -> UIResult<std::vector<GraphemeCluster>> override;
  [[nodiscard]] auto ResolveGlyph(const GlyphAtlasRequest &request) noexcept
      -> UIResult<GlyphAtlasEntry> override;
  [[nodiscard]] auto AtlasDiagnostics() const noexcept
      -> GlyphAtlasDiagnostics;

private:
  struct Impl;

  explicit NativeTextSystem(std::unique_ptr<Impl> implementation) noexcept;

  std::unique_ptr<Impl> m_impl;
};
} // namespace NGIN::UI
