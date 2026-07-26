#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Rendering.hpp>
#include <NGIN/UI/Text.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <memory>

namespace NGIN::UI {
/// @brief Font paths and bounded glyph-atlas storage for native text.
struct NativeTextCreateInfo final {
  NGIN::Text::String fontPath{};
  std::vector<NGIN::Text::String> fallbackFontPaths{};
  PixelSize atlasSize{1024, 1024};
  UInt32 maximumAtlasPages{4};
  bool includeBundledFallbacks{true};
};

/// @brief Identity, source, and observed coverage for one loaded font face.
struct FontFaceDiagnostics final {
  FontFaceHandle face{};
  NGIN::Text::String family{};
  NGIN::Text::String style{};
  NGIN::Text::String sourcePath{};
  bool fallback{false};
  UIntSize resolvedCodePointCount{0};
};

/// @brief Loaded faces plus unique fallback and unresolved code-point counts.
struct FontCoverageDiagnostics final {
  UIntSize fallbackCodePointCount{0};
  UIntSize missingCodePointCount{0};
  std::vector<UInt32> missingCodePoints{};
  std::vector<FontFaceDiagnostics> faces{};
};

/// @brief Stored glyph count and occupied pixels for one raster size.
struct GlyphAtlasSizeDiagnostics final {
  UInt32 pixelSize{0};
  UIntSize entryCount{0};
  UInt64 usedPixelArea{0};
};

/// @brief Capacity, activity, recycling, and recovery counters for glyph pages.
struct GlyphAtlasDiagnostics final {
  UInt64 hitCount{0};
  UInt64 missCount{0};
  UInt64 uploadCount{0};
  UInt64 evictionCount{0};
  UInt64 pageAllocationCount{0};
  UInt64 pageRecycleCount{0};
  UInt64 allocationFailureCount{0};
  UInt64 restorationCount{0};
  UIntSize entryCount{0};
  UIntSize pixelSizeCount{0};
  UIntSize pageCount{0};
  UIntSize peakPageCount{0};
  UIntSize maximumPageCount{0};
  UInt64 usedPixelArea{0};
  UInt64 capacityPixelArea{0};
  PixelSize atlasSize{};
  std::vector<GlyphAtlasSizeDiagnostics> pixelSizes{};
};

/// @brief FreeType and HarfBuzz implementation of font, shaping, and glyph services.
class NativeTextSystem final : public IFontProvider,
                               public ITextShaper,
                               public ITextLayout,
                               public ITextGeometry,
                               public IGraphemeSegmenter,
                               public IGlyphAtlas {
public:
  using ResourcesInvalidatedCallback = NGIN::Utilities::Callable<void()>;

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
  /// @brief Drops renderer-owned atlas pages after a device loss.
  void OnDeviceLost() noexcept;
  /// @brief Recreates empty atlas storage on a restored render device.
  [[nodiscard]] auto OnDeviceRestored(IRenderBackend &renderer) noexcept
      -> UIResult<void>;
  /// @brief Sets the callback used to invalidate windows after atlas reset.
  void SetResourcesInvalidatedCallback(ResourcesInvalidatedCallback callback);
  [[nodiscard]] auto AtlasDiagnostics() const noexcept
      -> GlyphAtlasDiagnostics;
  /// @brief Returns loaded font identities and observed fallback coverage.
  [[nodiscard]] auto CoverageDiagnostics() const noexcept
      -> FontCoverageDiagnostics;

private:
  struct Impl;

  explicit NativeTextSystem(std::unique_ptr<Impl> implementation) noexcept;

  std::unique_ptr<Impl> m_impl;
};
} // namespace NGIN::UI
