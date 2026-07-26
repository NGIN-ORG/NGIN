#include <NGIN/UI/NativeText.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(NGIN_UI_HAS_NATIVE_TEXT)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>
#endif

#if !defined(NGIN_UI_BUNDLED_FONT_PATH)
#define NGIN_UI_BUNDLED_FONT_PATH ""
#endif

#if !defined(NGIN_UI_BUNDLED_ARABIC_FONT_PATH)
#define NGIN_UI_BUNDLED_ARABIC_FONT_PATH ""
#endif

#if !defined(NGIN_UI_BUNDLED_SYMBOLS_FONT_PATH)
#define NGIN_UI_BUNDLED_SYMBOLS_FONT_PATH ""
#endif

namespace NGIN::UI {
namespace {
[[nodiscard]] auto NativeTextError(const UIErrorCode code, const char *message,
                                   const char *operation,
                                   const Int32 nativeCode = 0) -> UIError {
  return MakeUIError(code, message, "FreeType/HarfBuzz", operation, "Noto Sans",
                     nativeCode);
}

#if !defined(NGIN_UI_HAS_NATIVE_TEXT)
[[nodiscard]] auto Unavailable(const char *operation) -> UIError {
  return NativeTextError(
      UIErrorCode::Unsupported,
      "NGIN.UI was built without the native text implementation", operation);
}
#endif

[[nodiscard]] auto ResolveBundledFontPath(const char *fileName,
                                          const char *sourcePath)
    -> NGIN::Text::String {
  std::error_code error;
  const auto staged =
      std::filesystem::current_path(error) / "fonts" / "NGIN.UI" / fileName;
  if (!error && std::filesystem::is_regular_file(staged, error) && !error) {
    return NGIN::Text::String{staged.string()};
  }
  return NGIN::Text::String{sourcePath};
}

struct DecodedCodePoint final {
  UInt32 value{0};
  UIntSize byteOffset{0};
  UIntSize byteLength{0};
};

[[nodiscard]] auto DecodeUtf8(const NGIN::Text::String &text) noexcept
    -> UIResult<std::vector<DecodedCodePoint>> {
  try {
    std::vector<DecodedCodePoint> decoded;
    UIntSize offset = 0;
    while (offset < text.Size()) {
      const auto first = static_cast<UInt8>(text[offset]);
      UIntSize length = 0;
      UInt32 value = 0;
      if (first <= 0x7FU) {
        length = 1;
        value = first;
      } else if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        value = first & 0x1FU;
      } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        value = first & 0x0FU;
      } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        value = first & 0x07U;
      } else {
        return NativeTextError(UIErrorCode::TextShapingFailed,
                               "Text contains malformed UTF-8",
                               "SegmentGraphemes");
      }
      if (offset + length > text.Size()) {
        return NativeTextError(UIErrorCode::TextShapingFailed,
                               "Text contains truncated UTF-8",
                               "SegmentGraphemes");
      }
      for (UIntSize index = 1; index < length; ++index) {
        const auto continuation = static_cast<UInt8>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
          return NativeTextError(UIErrorCode::TextShapingFailed,
                                 "Text contains malformed UTF-8",
                                 "SegmentGraphemes");
        }
        value = (value << 6U) | (continuation & 0x3FU);
      }
      const auto minimum =
          length == 1
              ? 0U
              : (length == 2 ? 0x80U : (length == 3 ? 0x800U : 0x10000U));
      if (value < minimum || value > 0x10FFFFU ||
          (value >= 0xD800U && value <= 0xDFFFU)) {
        return NativeTextError(UIErrorCode::TextShapingFailed,
                               "Text contains non-canonical UTF-8",
                               "SegmentGraphemes");
      }
      decoded.push_back({value, offset, length});
      offset += length;
    }
    return decoded;
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to allocate grapheme metadata",
                           "SegmentGraphemes");
  }
}

[[nodiscard]] constexpr auto IsControl(const UInt32 value) noexcept -> bool {
  return value == 0x000AU || value == 0x000DU || value <= 0x001FU ||
         (value >= 0x007FU && value <= 0x009FU);
}

[[nodiscard]] constexpr auto IsExtend(const UInt32 value) noexcept -> bool {
  return (value >= 0x0300U && value <= 0x036FU) ||
         (value >= 0x1AB0U && value <= 0x1AFFU) ||
         (value >= 0x1DC0U && value <= 0x1DFFU) ||
         (value >= 0x20D0U && value <= 0x20FFU) ||
         (value >= 0xFE00U && value <= 0xFE0FU) ||
         (value >= 0xFE20U && value <= 0xFE2FU) ||
         (value >= 0x1F3FBU && value <= 0x1F3FFU) ||
         (value >= 0xE0100U && value <= 0xE01EFU);
}

[[nodiscard]] constexpr auto IsRegionalIndicator(const UInt32 value) noexcept
    -> bool {
  return value >= 0x1F1E6U && value <= 0x1F1FFU;
}

[[nodiscard]] constexpr auto IsHangulL(const UInt32 value) noexcept -> bool {
  return (value >= 0x1100U && value <= 0x115FU) ||
         (value >= 0xA960U && value <= 0xA97CU);
}

[[nodiscard]] constexpr auto IsHangulV(const UInt32 value) noexcept -> bool {
  return (value >= 0x1160U && value <= 0x11A7U) ||
         (value >= 0xD7B0U && value <= 0xD7C6U);
}

[[nodiscard]] constexpr auto IsHangulT(const UInt32 value) noexcept -> bool {
  return (value >= 0x11A8U && value <= 0x11FFU) ||
         (value >= 0xD7CBU && value <= 0xD7FBU);
}

[[nodiscard]] constexpr auto IsHangulSyllable(const UInt32 value) noexcept
    -> bool {
  return value >= 0xAC00U && value <= 0xD7A3U;
}

[[nodiscard]] constexpr auto IsHangulLv(const UInt32 value) noexcept -> bool {
  return IsHangulSyllable(value) && ((value - 0xAC00U) % 28U) == 0U;
}

[[nodiscard]] auto
SegmentDecoded(const std::vector<DecodedCodePoint> &decoded) noexcept
    -> UIResult<std::vector<GraphemeCluster>> {
  try {
    std::vector<GraphemeCluster> clusters;
    UIntSize regionalCount = 0;
    for (UIntSize index = 0; index < decoded.size(); ++index) {
      const auto &current = decoded[index];
      bool shouldBreak = index != 0;
      if (index != 0) {
        const auto previous = decoded[index - 1].value;
        if (previous == 0x000DU && current.value == 0x000AU) {
          shouldBreak = false;
        } else if (IsControl(previous) || IsControl(current.value)) {
          shouldBreak = true;
        } else if (IsExtend(current.value) || current.value == 0x200DU) {
          shouldBreak = false;
        } else if (previous == 0x200DU) {
          shouldBreak = false;
        } else if (IsHangulL(previous) &&
                   (IsHangulL(current.value) || IsHangulV(current.value) ||
                    IsHangulSyllable(current.value))) {
          shouldBreak = false;
        } else if ((IsHangulV(previous) || IsHangulLv(previous)) &&
                   (IsHangulV(current.value) || IsHangulT(current.value))) {
          shouldBreak = false;
        } else if ((IsHangulT(previous) ||
                    (IsHangulSyllable(previous) && !IsHangulLv(previous))) &&
                   IsHangulT(current.value)) {
          shouldBreak = false;
        } else if (IsRegionalIndicator(previous) &&
                   IsRegionalIndicator(current.value) &&
                   (regionalCount % 2U) == 1U) {
          shouldBreak = false;
        }
      }

      if (shouldBreak || clusters.empty()) {
        clusters.push_back({.byteOffset = current.byteOffset,
                            .byteLength = current.byteLength});
      } else {
        clusters.back().byteLength += current.byteLength;
      }
      if (IsRegionalIndicator(current.value)) {
        ++regionalCount;
      } else {
        regionalCount = 0;
      }
    }
    return clusters;
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to allocate grapheme clusters",
                           "SegmentGraphemes");
  }
}

#if defined(NGIN_UI_HAS_NATIVE_TEXT)
[[nodiscard]] auto RunByteLength(const PositionedShapedRun &run) noexcept
    -> UIntSize {
  if (run.run.graphemeClusters.empty()) {
    return 0;
  }
  const auto &last = run.run.graphemeClusters.back();
  return last.byteOffset + last.byteLength;
}

[[nodiscard]] auto RunCaretX(const PositionedShapedRun &run,
                             const UIntSize byteOffset) noexcept -> F32 {
  F32 pen = run.origin.x;
  if (run.run.direction == TextDirection::RightToLeft) {
    if (byteOffset >= RunByteLength(run)) {
      return run.origin.x;
    }
    for (const auto &glyph : run.run.glyphs) {
      const auto next = pen + glyph.advance.x;
      if (glyph.clusterByteOffset <= byteOffset) {
        return next;
      }
      pen = next;
    }
    return run.origin.x;
  }

  for (const auto &glyph : run.run.glyphs) {
    if (glyph.clusterByteOffset >= byteOffset) {
      return pen;
    }
    pen += glyph.advance.x;
  }
  return pen;
}
#endif
} // namespace

#if defined(NGIN_UI_HAS_NATIVE_TEXT)
struct NativeTextSystem::Impl final {
  struct Face final {
    FT_Face value{nullptr};
    hb_font_t *font{nullptr};
    NGIN::Text::String path{};
    bool fallback{false};
    std::unordered_set<UInt32> resolvedCodePoints{};
  };

  struct GlyphKey final {
    UInt32 face{0};
    UInt32 glyph{0};
    UInt32 pixelSize{0};

    [[nodiscard]] auto operator==(const GlyphKey &) const noexcept
        -> bool = default;
  };

  struct GlyphKeyHash final {
    [[nodiscard]] auto operator()(const GlyphKey &key) const noexcept
        -> std::size_t {
      return (static_cast<std::size_t>(key.face) << 48U) ^
             (static_cast<std::size_t>(key.glyph) << 16U) ^
             static_cast<std::size_t>(key.pixelSize);
    }
  };

  struct GlyphRecord final {
    GlyphAtlasEntry entry{};
    UInt32 pageIndex{0};
    UInt64 pixelArea{0};
    std::weak_ptr<const GlyphAtlasLease> lease{};
  };

  struct AtlasPage final {
    TextureHandle texture{};
    UInt32 x{1};
    UInt32 y{1};
    UInt32 rowHeight{0};
    UInt64 usedPixelArea{0};
    UInt64 lastUse{0};
    std::vector<GlyphKey> keys{};
  };

  IRenderBackend *renderer{nullptr};
  FT_Library library{nullptr};
  std::vector<Face> faces{};
  PixelSize atlasSize{};
  UInt32 maximumAtlasPages{4};
  UInt64 useClock{0};
  std::vector<AtlasPage> pages{};
  std::unordered_map<GlyphKey, GlyphRecord, GlyphKeyHash> glyphs{};
  std::unordered_set<UInt32> fallbackCodePoints{};
  std::unordered_set<UInt32> missingCodePoints{};
  GlyphAtlasDiagnostics diagnostics{};
  NativeTextSystem::ResourcesInvalidatedCallback invalidated{};

  ~Impl() {
    DestroyPages(false);
    for (auto &face : faces) {
      if (face.font != nullptr) {
        hb_font_destroy(face.font);
      }
      if (face.value != nullptr) {
        static_cast<void>(FT_Done_Face(face.value));
      }
    }
    if (library != nullptr) {
      static_cast<void>(FT_Done_FreeType(library));
    }
  }

  [[nodiscard]] auto CreateAtlasTexture() noexcept -> UIResult<TextureHandle> {
    if (renderer == nullptr) {
      return NativeTextError(
          UIErrorCode::InvalidState,
          "Glyph textures are unavailable while the render device is lost",
          "CreateGlyphAtlasPage");
    }
    return renderer->CreateTexture(TextureCreateInfo{
        .size = atlasSize,
        .format = TextureFormat::R8,
        .filter = TextureFilter::Nearest,
    });
  }

  [[nodiscard]] auto AddPage() -> UIResult<UInt32> {
    auto texture = CreateAtlasTexture();
    if (!texture) {
      return std::move(texture).Error();
    }
    try {
      const auto index = static_cast<UInt32>(pages.size());
      pages.push_back(AtlasPage{
          .texture = texture.Value(),
          .lastUse = ++useClock,
      });
      ++diagnostics.pageAllocationCount;
      diagnostics.peakPageCount =
          std::max(diagnostics.peakPageCount, pages.size());
      return index;
    } catch (...) {
      static_cast<void>(renderer->DestroyTexture(texture.Value()));
      throw;
    }
  }

  [[nodiscard]] auto CanPlace(const AtlasPage &page, const UInt32 width,
                              const UInt32 height) const noexcept -> bool {
    auto x = page.x;
    auto y = page.y;
    if (x + width + 1U > atlasSize.width) {
      x = 1;
      y += page.rowHeight + 1U;
    }
    return x + width + 1U <= atlasSize.width &&
           y + height + 1U <= atlasSize.height;
  }

  [[nodiscard]] auto PageIsUnused(const AtlasPage &page) const noexcept
      -> bool {
    return std::all_of(page.keys.begin(), page.keys.end(),
                       [this](const GlyphKey &key) {
                         const auto found = glyphs.find(key);
                         return found == glyphs.end() ||
                                found->second.lease.expired();
                       });
  }

  [[nodiscard]] auto RecyclePage(const UInt32 pageIndex) -> UIResult<void> {
    auto &page = pages[pageIndex];
    const auto texture = page.texture;
    for (const auto &key : page.keys) {
      const auto found = glyphs.find(key);
      if (found != glyphs.end() && found->second.pageIndex == pageIndex) {
        glyphs.erase(found);
        ++diagnostics.evictionCount;
      }
    }
    diagnostics.usedPixelArea -=
        std::min(diagnostics.usedPixelArea, page.usedPixelArea);
    page = AtlasPage{
        .texture = texture,
        .lastUse = ++useClock,
    };
    ++diagnostics.pageRecycleCount;
    return {};
  }

  [[nodiscard]] auto AcquirePage(const UInt32 width, const UInt32 height)
      -> UIResult<UInt32> {
    if (width + 2U > atlasSize.width || height + 2U > atlasSize.height) {
      ++diagnostics.allocationFailureCount;
      return NativeTextError(
          UIErrorCode::InvalidArgument,
          "A rasterized glyph is larger than one glyph atlas page",
          "ResolveGlyph");
    }
    for (UInt32 index = 0; index < pages.size(); ++index) {
      if (CanPlace(pages[index], width, height)) {
        return index;
      }
    }
    if (pages.size() < maximumAtlasPages) {
      return AddPage();
    }

    UInt32 candidate = std::numeric_limits<UInt32>::max();
    UInt64 oldestUse = std::numeric_limits<UInt64>::max();
    for (UInt32 index = 0; index < pages.size(); ++index) {
      if (PageIsUnused(pages[index]) && pages[index].lastUse < oldestUse) {
        candidate = index;
        oldestUse = pages[index].lastUse;
      }
    }
    if (candidate != std::numeric_limits<UInt32>::max()) {
      auto recycled = RecyclePage(candidate);
      if (!recycled) {
        ++diagnostics.allocationFailureCount;
        return std::move(recycled).Error();
      }
      return candidate;
    }

    ++diagnostics.allocationFailureCount;
    return NativeTextError(
        UIErrorCode::OutOfMemory,
        "All glyph atlas pages are still used by visible text",
        "ResolveGlyph");
  }

  void Place(AtlasPage &page, const UInt32 width) noexcept {
    if (page.x + width + 1U > atlasSize.width) {
      page.x = 1;
      page.y += page.rowHeight + 1U;
      page.rowHeight = 0;
    }
  }

  void DestroyPages(const bool countEvictions) noexcept {
    if (countEvictions) {
      diagnostics.evictionCount += glyphs.size();
    }
    if (renderer != nullptr) {
      for (const auto &page : pages) {
        if (page.texture) {
          static_cast<void>(renderer->DestroyTexture(page.texture));
        }
      }
    }
    glyphs.clear();
    pages.clear();
    diagnostics.entryCount = 0;
    diagnostics.usedPixelArea = 0;
  }

  void NotifyResourcesInvalidated() noexcept {
    if (!invalidated) {
      return;
    }
    try {
      invalidated();
    } catch (...) {
    }
  }

  [[nodiscard]] auto FaceFor(const FontFaceHandle handle) noexcept -> Face * {
    return handle.generation == 1 && handle.index < faces.size()
               ? &faces[handle.index]
               : nullptr;
  }

  [[nodiscard]] auto SetPixelSize(Face &face, const UInt32 size,
                                  const char *operation) -> UIResult<void> {
    const auto error = FT_Set_Pixel_Sizes(face.value, 0, std::max(1U, size));
    if (error != 0) {
      return NativeTextError(UIErrorCode::TextShapingFailed,
                             "FreeType could not set the requested font size",
                             operation, error);
    }
    hb_ft_font_changed(face.font);
    return {};
  }

  [[nodiscard]] auto MetricsForSize(Face &face, const F32 size)
      -> UIResult<FontMetrics> {
    auto sized = SetPixelSize(
        face, static_cast<UInt32>(std::max(1.0F, std::round(size))),
        "FontMetrics");
    if (!sized) {
      return sized.Error();
    }
    const auto scale = 1.0F / 64.0F;
    const auto ascender =
        static_cast<F32>(face.value->size->metrics.ascender) * scale;
    const auto descender =
        -static_cast<F32>(face.value->size->metrics.descender) * scale;
    const auto height =
        static_cast<F32>(face.value->size->metrics.height) * scale;
    return FontMetrics{
        .ascender = ascender,
        .descender = descender,
        .lineGap = std::max(0.0F, height - ascender - descender),
        .unitsPerEm = static_cast<F32>(face.value->units_per_EM),
    };
  }
};
#else
struct NativeTextSystem::Impl final {};
#endif

NativeTextSystem::NativeTextSystem(
    std::unique_ptr<Impl> implementation) noexcept
    : m_impl(std::move(implementation)) {}

NativeTextSystem::~NativeTextSystem() = default;

auto NativeTextSystem::BundledFontPath() noexcept -> const char * {
  return NGIN_UI_BUNDLED_FONT_PATH;
}

auto NativeTextSystem::Create(IRenderBackend &renderer,
                              const NativeTextCreateInfo &info) noexcept
    -> UIResult<std::unique_ptr<NativeTextSystem>> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    if (info.atlasSize.IsEmpty() || info.maximumAtlasPages == 0) {
      return NativeTextError(UIErrorCode::InvalidArgument,
                             "Glyph atlas dimensions and page count must be non-zero",
                             "CreateTextSystem");
    }
    auto implementation = std::make_unique<Impl>();
    implementation->renderer = &renderer;
    implementation->atlasSize = info.atlasSize;
    implementation->maximumAtlasPages = info.maximumAtlasPages;
    implementation->diagnostics.atlasSize = info.atlasSize;
    implementation->diagnostics.maximumPageCount = info.maximumAtlasPages;
    auto error = FT_Init_FreeType(&implementation->library);
    if (error != 0) {
      return NativeTextError(UIErrorCode::ResourceFailed,
                             "FreeType initialization failed",
                             "CreateTextSystem", error);
    }
    const auto loadFace =
        [&implementation](const NGIN::Text::String &path,
                          const bool fallback) -> UIResult<void> {
      Impl::Face face{};
      const auto loadError =
          FT_New_Face(implementation->library, path.CStr(), 0, &face.value);
      if (loadError != 0) {
        return NativeTextError(UIErrorCode::ResourceFailed,
                               "A configured font could not be loaded",
                               "CreateTextSystem", loadError);
      }
      face.font = hb_ft_font_create_referenced(face.value);
      if (face.font == nullptr) {
        static_cast<void>(FT_Done_Face(face.value));
        return NativeTextError(UIErrorCode::OutOfMemory,
                               "HarfBuzz font creation failed",
                               "CreateTextSystem");
      }
      face.path = path;
      face.fallback = fallback;
      implementation->faces.push_back(std::move(face));
      return {};
    };

    std::vector<NGIN::Text::String> loadedPaths;
    const auto loadUnique =
        [&loadFace, &loadedPaths](const NGIN::Text::String &path,
                                  const bool fallback) -> UIResult<void> {
      if (path.Empty() ||
          std::find(loadedPaths.begin(), loadedPaths.end(), path) !=
              loadedPaths.end()) {
        return {};
      }
      auto loaded = loadFace(path, fallback);
      if (loaded) {
        loadedPaths.push_back(path);
      }
      return loaded;
    };

    const auto primary =
        info.fontPath.Empty()
            ? ResolveBundledFontPath("NotoSans-Variable.ttf",
                                     NGIN_UI_BUNDLED_FONT_PATH)
            : info.fontPath;
    auto loaded = loadUnique(primary, false);
    if (!loaded) {
      return loaded.Error();
    }
    for (const auto &fallback : info.fallbackFontPaths) {
      loaded = loadUnique(fallback, true);
      if (!loaded) {
        return loaded.Error();
      }
    }
    if (info.includeBundledFallbacks) {
      constexpr std::array bundledFallbacks{
          std::pair{"NotoSansArabic-Variable.ttf",
                    NGIN_UI_BUNDLED_ARABIC_FONT_PATH},
          std::pair{"NotoSansSymbols2-Regular.ttf",
                    NGIN_UI_BUNDLED_SYMBOLS_FONT_PATH},
      };
      for (const auto &[fileName, sourcePath] : bundledFallbacks) {
        loaded =
            loadUnique(ResolveBundledFontPath(fileName, sourcePath), true);
        if (!loaded) {
          return loaded.Error();
        }
      }
    }
    auto page = implementation->AddPage();
    if (!page) {
      return page.Error();
    }
    return std::unique_ptr<NativeTextSystem>{
        new NativeTextSystem{std::move(implementation)}};
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Native text service allocation failed",
                           "CreateTextSystem");
  }
#else
  static_cast<void>(renderer);
  static_cast<void>(info);
  return Unavailable("CreateTextSystem");
#endif
}

auto NativeTextSystem::ResolveFont(const FontRequest &request) noexcept
    -> UIResult<FontFaceHandle> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  for (const auto &family : request.families) {
    for (UInt32 index = 0; index < m_impl->faces.size(); ++index) {
      const auto *name = m_impl->faces[index].value->family_name;
      if (name != nullptr && family.View() == std::string_view{name}) {
        return FontFaceHandle{index, 1};
      }
    }
  }
  return FontFaceHandle{0, 1};
#else
  static_cast<void>(request);
  return Unavailable("ResolveFont");
#endif
}

auto NativeTextSystem::Metrics(const FontFaceHandle face,
                               const F32 fontSize) noexcept
    -> UIResult<FontMetrics> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  auto *resolved = m_impl->FaceFor(face);
  if (resolved == nullptr || !std::isfinite(fontSize) || fontSize <= 0.0F) {
    return NativeTextError(UIErrorCode::InvalidArgument,
                           "A live font face and positive size are required",
                           "FontMetrics");
  }
  return m_impl->MetricsForSize(*resolved, fontSize);
#else
  static_cast<void>(face);
  static_cast<void>(fontSize);
  return Unavailable("FontMetrics");
#endif
}

auto NativeTextSystem::Segment(const NGIN::Text::String &text) noexcept
    -> UIResult<std::vector<GraphemeCluster>> {
  auto decoded = DecodeUtf8(text);
  if (!decoded) {
    return decoded.Error();
  }
  return SegmentDecoded(decoded.Value());
}

auto NativeTextSystem::Shape(const TextRun &run,
                             const FontFaceHandle face) noexcept
    -> UIResult<ShapedRun> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    auto *resolved = m_impl->FaceFor(face);
    if (resolved == nullptr || !std::isfinite(run.fontSize) ||
        run.fontSize <= 0.0F) {
      return NativeTextError(UIErrorCode::InvalidArgument,
                             "A live font face and positive size are required",
                             "ShapeText");
    }
    auto graphemes = Segment(run.text);
    if (!graphemes) {
      return graphemes.Error();
    }
    auto metrics = m_impl->MetricsForSize(*resolved, run.fontSize);
    if (!metrics) {
      return metrics.Error();
    }

    auto *buffer = hb_buffer_create();
    if (buffer == nullptr) {
      return NativeTextError(UIErrorCode::OutOfMemory,
                             "HarfBuzz buffer creation failed", "ShapeText");
    }
    struct BufferGuard final {
      hb_buffer_t *value;
      ~BufferGuard() { hb_buffer_destroy(value); }
    } guard{buffer};

    hb_buffer_set_cluster_level(buffer,
                                HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_add_utf8(buffer, run.text.CStr(),
                       static_cast<int>(run.text.Size()), 0,
                       static_cast<int>(run.text.Size()));
    if (run.direction == TextDirection::LeftToRight) {
      hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
    } else if (run.direction == TextDirection::RightToLeft) {
      hb_buffer_set_direction(buffer, HB_DIRECTION_RTL);
    }
    if (!run.language.Empty()) {
      hb_buffer_set_language(
          buffer,
          hb_language_from_string(run.language.CStr(),
                                  static_cast<int>(run.language.Size())));
    }
    if (!run.script.Empty()) {
      hb_buffer_set_script(
          buffer, hb_script_from_string(run.script.CStr(),
                                        static_cast<int>(run.script.Size())));
    }
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(resolved->font, buffer, nullptr, 0);

    unsigned int glyphCount = 0;
    const auto *infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
    const auto *positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);
    ShapedRun shaped{
        .fontFace = face,
        .direction = hb_buffer_get_direction(buffer) == HB_DIRECTION_RTL
                         ? TextDirection::RightToLeft
                         : TextDirection::LeftToRight,
        .metrics = metrics.Value(),
        .graphemeClusters = std::move(graphemes).Value(),
    };
    shaped.glyphs.reserve(glyphCount);
    F32 width = 0.0F;
    for (unsigned int index = 0; index < glyphCount; ++index) {
      const auto advanceX =
          static_cast<F32>(positions[index].x_advance) / 64.0F;
      shaped.glyphs.push_back(ShapedGlyph{
          .glyphIndex = infos[index].codepoint,
          .clusterByteOffset = infos[index].cluster,
          .offset =
              Point{
                  static_cast<F32>(positions[index].x_offset) / 64.0F,
                  -static_cast<F32>(positions[index].y_offset) / 64.0F,
              },
          .advance =
              Point{
                  advanceX,
                  -static_cast<F32>(positions[index].y_advance) / 64.0F,
              },
      });
      width += std::abs(advanceX);
    }
    shaped.size =
        Size{width, shaped.metrics.ascender + shaped.metrics.descender +
                        shaped.metrics.lineGap};
    return shaped;
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to allocate shaped glyph data", "ShapeText");
  }
#else
  static_cast<void>(run);
  static_cast<void>(face);
  return Unavailable("ShapeText");
#endif
}

auto NativeTextSystem::LayoutParagraph(const ParagraphRequest &request) noexcept
    -> UIResult<ParagraphLayout> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    if ((!std::isfinite(request.maximumWidth) &&
         request.maximumWidth != std::numeric_limits<F32>::infinity()) ||
        request.maximumWidth < 0.0F || !std::isfinite(request.lineHeight) ||
        request.lineHeight < 0.0F) {
      return NativeTextError(
          UIErrorCode::InvalidArgument,
          "Paragraph dimensions must be finite and non-negative",
          "LayoutParagraph");
    }

    struct Token final {
      TextRun style{};
      NGIN::Text::String text{};
      UIntSize byteOffset{0};
      UIntSize byteLength{0};
      bool mandatoryBreak{false};
    };
    struct ShapedToken final {
      std::vector<PositionedShapedRun> runs{};
      F32 width{0.0F};
      F32 ascender{0.0F};
      F32 descender{0.0F};
    };

    const auto isWhitespace = [](const UInt32 value) noexcept {
      return value == 0x0009U || value == 0x0020U || value == 0x00A0U ||
             value == 0x1680U || (value >= 0x2000U && value <= 0x200AU) ||
             value == 0x202FU || value == 0x205FU || value == 0x3000U;
    };
    const auto isCjk = [](const UInt32 value) noexcept {
      return (value >= 0x2E80U && value <= 0x9FFFU) ||
             (value >= 0xAC00U && value <= 0xD7A3U) ||
             (value >= 0xF900U && value <= 0xFAFFU) ||
             (value >= 0x20000U && value <= 0x323AFU);
    };
    const auto breaksAfter = [&isWhitespace,
                              &isCjk](const UInt32 value) noexcept {
      return isWhitespace(value) || isCjk(value) || value == 0x002DU ||
             value == 0x058AU || value == 0x2010U || value == 0x2013U ||
             value == 0x002FU || value == 0x005CU;
    };

    std::vector<Token> tokens;
    UIntSize paragraphByteOffset = 0;
    for (const auto &sourceRun : request.runs) {
      auto decoded = DecodeUtf8(sourceRun.text);
      if (!decoded) {
        return decoded.Error();
      }
      auto clusters = SegmentDecoded(decoded.Value());
      if (!clusters) {
        return clusters.Error();
      }
      UIntSize tokenStart = 0;
      UIntSize tokenLength = 0;
      const auto flushToken = [&]() {
        if (tokenLength == 0) {
          return;
        }
        tokens.push_back(Token{
            .style = sourceRun,
            .text = sourceRun.text.Substr(tokenStart, tokenLength),
            .byteOffset = paragraphByteOffset + tokenStart,
            .byteLength = tokenLength,
        });
        tokenLength = 0;
      };
      for (const auto &cluster : clusters.Value()) {
        const auto decodedPoint =
            std::find_if(decoded.Value().begin(), decoded.Value().end(),
                         [&cluster](const DecodedCodePoint &point) {
                           return point.byteOffset == cluster.byteOffset;
                         });
        const auto value =
            decodedPoint == decoded.Value().end() ? 0U : decodedPoint->value;
        if (value == 0x000AU || value == 0x000DU) {
          flushToken();
          auto newlineLength = cluster.byteLength;
          if (value == 0x000DU &&
              cluster.byteOffset + cluster.byteLength < sourceRun.text.Size() &&
              sourceRun.text[cluster.byteOffset + cluster.byteLength] == '\n') {
            newlineLength += 1;
          }
          tokens.push_back(Token{
              .style = sourceRun,
              .byteOffset = paragraphByteOffset + cluster.byteOffset,
              .byteLength = newlineLength,
              .mandatoryBreak = true,
          });
          continue;
        }
        if (tokenLength == 0) {
          tokenStart = cluster.byteOffset;
        }
        tokenLength += cluster.byteLength;
        if (breaksAfter(value)) {
          flushToken();
        }
      }
      flushToken();
      paragraphByteOffset += sourceRun.text.Size();
    }

    const auto faceForCluster =
        [this](const NGIN::Text::String &text, const GraphemeCluster &cluster,
               const FontFaceHandle preferred) -> FontFaceHandle {
      auto decoded =
          DecodeUtf8(text.Substr(cluster.byteOffset, cluster.byteLength));
      if (!decoded) {
        return preferred;
      }
      const auto supports = [&decoded](const Impl::Face &face) {
        for (const auto &point : decoded.Value()) {
          if (IsControl(point.value) || point.value == 0x200DU ||
              point.value == 0xFE0FU) {
            continue;
          }
          if (FT_Get_Char_Index(face.value, point.value) == 0) {
            return false;
          }
        }
        return true;
      };
      const auto recordResolved =
          [this, &decoded, preferred](const UInt32 faceIndex) {
        for (const auto &point : decoded.Value()) {
          if (IsControl(point.value) || point.value == 0x200DU ||
              point.value == 0xFE0FU) {
            continue;
          }
          m_impl->faces[faceIndex].resolvedCodePoints.insert(point.value);
          if (faceIndex != preferred.index) {
            m_impl->fallbackCodePoints.insert(point.value);
          }
        }
      };
      if (preferred.index < m_impl->faces.size() &&
          supports(m_impl->faces[preferred.index])) {
        recordResolved(preferred.index);
        return preferred;
      }
      for (UInt32 index = 0; index < m_impl->faces.size(); ++index) {
        if (supports(m_impl->faces[index])) {
          recordResolved(index);
          return FontFaceHandle{index, 1};
        }
      }
      for (const auto &point : decoded.Value()) {
        if (!IsControl(point.value) && point.value != 0x200DU &&
            point.value != 0xFE0FU) {
          m_impl->missingCodePoints.insert(point.value);
        }
      }
      return preferred;
    };

    const auto shapeToken =
        [this, &faceForCluster](const Token &token) -> UIResult<ShapedToken> {
      ShapedToken shapedToken;
      auto preferred = ResolveFont(token.style.font);
      if (!preferred) {
        return preferred.Error();
      }
      auto clusters = Segment(token.text);
      if (!clusters) {
        return clusters.Error();
      }
      UIntSize spanStart = 0;
      UIntSize spanLength = 0;
      auto spanFace = preferred.Value();
      const auto flushSpan = [&]() -> UIResult<void> {
        if (spanLength == 0) {
          return {};
        }
        auto span = token.style;
        span.text = token.text.Substr(spanStart, spanLength);
        auto shaped = Shape(span, spanFace);
        if (!shaped) {
          return shaped.Error();
        }
        shapedToken.width += shaped.Value().size.width;
        shapedToken.ascender =
            std::max(shapedToken.ascender, shaped.Value().metrics.ascender);
        shapedToken.descender =
            std::max(shapedToken.descender, shaped.Value().metrics.descender +
                                                shaped.Value().metrics.lineGap);
        shapedToken.runs.push_back(PositionedShapedRun{
            .run = std::move(shaped).Value(),
            .fontSize = span.fontSize,
            .byteOffset = token.byteOffset + spanStart,
        });
        spanLength = 0;
        return {};
      };
      for (const auto &cluster : clusters.Value()) {
        const auto face =
            faceForCluster(token.text, cluster, preferred.Value());
        if (spanLength != 0 && face != spanFace) {
          auto flushed = flushSpan();
          if (!flushed) {
            return flushed.Error();
          }
        }
        if (spanLength == 0) {
          spanStart = cluster.byteOffset;
          spanFace = face;
        }
        spanLength += cluster.byteLength;
      }
      auto flushed = flushSpan();
      if (!flushed) {
        return flushed.Error();
      }
      return shapedToken;
    };

    ParagraphLayout paragraph;
    std::vector<PositionedShapedRun> lineRuns;
    F32 lineWidth = 0.0F;
    F32 lineAscender = 0.0F;
    F32 lineDescender = 0.0F;
    F32 y = 0.0F;
    F32 maximumLineWidth = 0.0F;
    UIntSize lineStartByte = 0;
    UIntSize lineEndByte = 0;
    const auto finiteMaximum = std::isfinite(request.maximumWidth);

    const auto defaultMetrics = [&]() -> FontMetrics {
      if (!request.runs.empty()) {
        auto face = ResolveFont(request.runs.front().font);
        if (face) {
          auto metrics = Metrics(face.Value(), request.runs.front().fontSize);
          if (metrics) {
            return metrics.Value();
          }
        }
      }
      return FontMetrics{
          .ascender = 11.0F,
          .descender = 3.0F,
      };
    }();
    const auto finishLine = [&](const UIntSize nextStart) {
      const auto ascender =
          lineAscender > 0.0F ? lineAscender : defaultMetrics.ascender;
      const auto descender = lineDescender > 0.0F ? lineDescender
                                                  : defaultMetrics.descender +
                                                        defaultMetrics.lineGap;
      const auto naturalHeight = ascender + descender;
      const auto height =
          request.lineHeight > 0.0F ? request.lineHeight : naturalHeight;
      const auto baseline =
          y + ascender + std::max(0.0F, height - naturalHeight) * 0.5F;
      F32 alignmentOffset = 0.0F;
      if (finiteMaximum && lineWidth < request.maximumWidth) {
        if (request.alignment == TextAlignment::Center) {
          alignmentOffset = (request.maximumWidth - lineWidth) * 0.5F;
        } else if (request.alignment == TextAlignment::End) {
          alignmentOffset = request.maximumWidth - lineWidth;
        }
      }
      const auto firstRun = paragraph.runs.size();
      for (auto &run : lineRuns) {
        run.origin.x += alignmentOffset;
        run.origin.y = baseline;
        run.lineIndex = paragraph.lines.size();
        paragraph.runs.push_back(std::move(run));
      }
      paragraph.lines.push_back(ParagraphLine{
          .firstRun = firstRun,
          .runCount = paragraph.runs.size() - firstRun,
          .bounds = Rect{alignmentOffset, y, lineWidth, height},
          .baseline = baseline,
          .byteOffset = lineStartByte,
          .byteLength = lineEndByte - lineStartByte,
      });
      maximumLineWidth = std::max(maximumLineWidth, lineWidth);
      y += height;
      lineRuns.clear();
      lineWidth = 0.0F;
      lineAscender = 0.0F;
      lineDescender = 0.0F;
      lineStartByte = nextStart;
      lineEndByte = nextStart;
    };
    const auto appendToken = [&](const Token &token, ShapedToken shaped) {
      if (request.wrapping == TextWrapping::Wrap && finiteMaximum &&
          !lineRuns.empty() &&
          lineWidth + shaped.width > request.maximumWidth) {
        finishLine(token.byteOffset);
      }
      for (auto &run : shaped.runs) {
        run.origin.x = lineWidth;
        lineWidth += run.run.size.width;
        lineRuns.push_back(std::move(run));
      }
      lineAscender = std::max(lineAscender, shaped.ascender);
      lineDescender = std::max(lineDescender, shaped.descender);
      lineEndByte = token.byteOffset + token.byteLength;
    };

    for (const auto &token : tokens) {
      if (token.mandatoryBreak) {
        lineEndByte = token.byteOffset;
        finishLine(token.byteOffset + token.byteLength);
        continue;
      }
      auto shaped = shapeToken(token);
      if (!shaped) {
        return shaped.Error();
      }
      if (request.wrapping == TextWrapping::Wrap && finiteMaximum &&
          shaped.Value().width > request.maximumWidth) {
        auto clusters = Segment(token.text);
        if (!clusters) {
          return clusters.Error();
        }
        for (const auto &cluster : clusters.Value()) {
          Token part{
              .style = token.style,
              .text = token.text.Substr(cluster.byteOffset, cluster.byteLength),
              .byteOffset = token.byteOffset + cluster.byteOffset,
              .byteLength = cluster.byteLength,
          };
          auto shapedPart = shapeToken(part);
          if (!shapedPart) {
            return shapedPart.Error();
          }
          appendToken(part, std::move(shapedPart).Value());
        }
      } else {
        appendToken(token, std::move(shaped).Value());
      }
    }
    if (paragraph.lines.empty() || lineStartByte <= paragraphByteOffset) {
      lineEndByte = std::max(lineEndByte, paragraphByteOffset);
      finishLine(paragraphByteOffset);
    }
    paragraph.size = Size{
        finiteMaximum ? std::min(maximumLineWidth, request.maximumWidth)
                      : maximumLineWidth,
        y,
    };
    paragraph.byteLength = paragraphByteOffset;
    return paragraph;
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to allocate paragraph layout",
                           "LayoutParagraph");
  }
#else
  static_cast<void>(request);
  return Unavailable("LayoutParagraph");
#endif
}

auto NativeTextSystem::CaretRect(const ParagraphLayout &paragraph,
                                 UIntSize byteOffset) noexcept
    -> UIResult<Rect> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  if (byteOffset > paragraph.byteLength) {
    return NativeTextError(UIErrorCode::InvalidArgument,
                           "Caret byte offset is outside the paragraph",
                           "CaretRect");
  }
  for (const auto &run : paragraph.runs) {
    if (byteOffset == run.byteOffset) {
      return Rect{RunCaretX(run, 0), run.origin.y - run.run.metrics.ascender,
                  1.0F,
                  run.run.metrics.ascender + run.run.metrics.descender +
                      run.run.metrics.lineGap};
    }
  }
  for (const auto &run : paragraph.runs) {
    const auto length = RunByteLength(run);
    if (byteOffset > run.byteOffset && byteOffset <= run.byteOffset + length) {
      const auto local = std::min(byteOffset - run.byteOffset, length);
      return Rect{RunCaretX(run, local),
                  run.origin.y - run.run.metrics.ascender, 1.0F,
                  run.run.metrics.ascender + run.run.metrics.descender +
                      run.run.metrics.lineGap};
    }
  }
  for (UIntSize index = 0; index < paragraph.lines.size(); ++index) {
    const auto &line = paragraph.lines[index];
    const auto lineEnd = line.byteOffset + line.byteLength;
    const auto nextStart = index + 1 < paragraph.lines.size()
                               ? paragraph.lines[index + 1].byteOffset
                               : paragraph.byteLength;
    if (byteOffset >= line.byteOffset &&
        (byteOffset < nextStart || (index + 1 == paragraph.lines.size() &&
                                    byteOffset <= paragraph.byteLength))) {
      const auto atStart = byteOffset <= line.byteOffset;
      return Rect{
          atStart ? line.bounds.x : line.bounds.x + line.bounds.width,
          line.bounds.y,
          1.0F,
          line.bounds.height,
      };
    }
    if (byteOffset == lineEnd) {
      return Rect{line.bounds.x + line.bounds.width, line.bounds.y, 1.0F,
                  line.bounds.height};
    }
  }
  return NativeTextError(UIErrorCode::InvalidArgument,
                         "Caret byte offset is outside the paragraph",
                         "CaretRect");
#else
  static_cast<void>(paragraph);
  static_cast<void>(byteOffset);
  return Unavailable("CaretRect");
#endif
}

auto NativeTextSystem::RangeRects(const ParagraphLayout &paragraph,
                                  const UIntSize byteOffset,
                                  const UIntSize byteLength) noexcept
    -> UIResult<std::vector<Rect>> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    if (byteLength > std::numeric_limits<UIntSize>::max() - byteOffset) {
      return NativeTextError(UIErrorCode::InvalidArgument,
                             "Text range overflows the paragraph",
                             "RangeRects");
    }
    const auto rangeEnd = byteOffset + byteLength;
    std::vector<Rect> rectangles;
    for (const auto &run : paragraph.runs) {
      const auto length = RunByteLength(run);
      const auto runEnd = run.byteOffset + length;
      const auto intersectionStart = std::max(byteOffset, run.byteOffset);
      const auto intersectionEnd = std::min(rangeEnd, runEnd);
      if (intersectionStart < intersectionEnd) {
        const auto startX = RunCaretX(run, intersectionStart - run.byteOffset);
        const auto endX = RunCaretX(run, intersectionEnd - run.byteOffset);
        rectangles.push_back(Rect{
            std::min(startX, endX),
            run.origin.y - run.run.metrics.ascender,
            std::abs(endX - startX),
            run.run.metrics.ascender + run.run.metrics.descender +
                run.run.metrics.lineGap,
        });
      }
    }
    if (rangeEnd > paragraph.byteLength) {
      return NativeTextError(UIErrorCode::InvalidArgument,
                             "Text range is outside the paragraph",
                             "RangeRects");
    }
    return rectangles;
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to allocate text range geometry",
                           "RangeRects");
  }
#else
  static_cast<void>(paragraph);
  static_cast<void>(byteOffset);
  static_cast<void>(byteLength);
  return Unavailable("RangeRects");
#endif
}

auto NativeTextSystem::ResolveGlyph(const GlyphAtlasRequest &request) noexcept
    -> UIResult<GlyphAtlasEntry> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    if (m_impl->renderer == nullptr) {
      return NativeTextError(
          UIErrorCode::InvalidState,
          "Glyph textures are unavailable while the render device is lost",
          "ResolveGlyph");
    }
    auto *face = m_impl->FaceFor(request.fontFace);
    if (face == nullptr || !std::isfinite(request.fontSize) ||
        request.fontSize <= 0.0F || !std::isfinite(request.scaleFactor) ||
        request.scaleFactor <= 0.0F) {
      return NativeTextError(
          UIErrorCode::InvalidArgument,
          "A live face, positive size, and scale are required", "ResolveGlyph");
    }
    const auto pixelSize = static_cast<UInt32>(
        std::max(1.0F, std::round(request.fontSize * request.scaleFactor)));
    const Impl::GlyphKey key{request.fontFace.index, request.glyphIndex,
                             pixelSize};
    if (const auto found = m_impl->glyphs.find(key);
        found != m_impl->glyphs.end()) {
      ++m_impl->diagnostics.hitCount;
      auto lease = found->second.lease.lock();
      if (!lease) {
        lease = std::make_shared<GlyphAtlasLease>();
        found->second.lease = lease;
      }
      auto entry = found->second.entry;
      entry.lease = std::move(lease);
      auto &page = m_impl->pages[found->second.pageIndex];
      page.lastUse = ++m_impl->useClock;
      return entry;
    }
    ++m_impl->diagnostics.missCount;
    auto sized = m_impl->SetPixelSize(*face, pixelSize, "ResolveGlyph");
    if (!sized) {
      return sized.Error();
    }
    auto error =
        FT_Load_Glyph(face->value, request.glyphIndex, FT_LOAD_DEFAULT);
    if (error == 0) {
      error = FT_Render_Glyph(face->value->glyph, FT_RENDER_MODE_NORMAL);
    }
    if (error != 0) {
      return NativeTextError(UIErrorCode::ResourceFailed,
                             "FreeType could not rasterize a glyph",
                             "ResolveGlyph", error);
    }
    const auto &bitmap = face->value->glyph->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0) {
      return GlyphAtlasEntry{};
    }
    const auto width = static_cast<UInt32>(bitmap.width);
    const auto height = static_cast<UInt32>(bitmap.rows);
    auto pageIndex = m_impl->AcquirePage(width, height);
    if (!pageIndex) {
      return std::move(pageIndex).Error();
    }
    auto &page = m_impl->pages[pageIndex.Value()];
    m_impl->Place(page, width);

    std::vector<Byte> pixels(static_cast<UIntSize>(width) *
                             static_cast<UIntSize>(height));
    const auto pitch = static_cast<UIntSize>(std::abs(bitmap.pitch));
    for (UInt32 row = 0; row < height; ++row) {
      const auto sourceRow = bitmap.pitch >= 0 ? row : (height - row - 1);
      const auto *source =
          bitmap.buffer + static_cast<UIntSize>(sourceRow) * pitch;
      auto *destination = pixels.data() + static_cast<UIntSize>(row * width);
      for (UInt32 column = 0; column < width; ++column) {
        destination[column] = static_cast<Byte>(source[column]);
      }
    }
    auto updated = m_impl->renderer->UpdateTexture(
        page.texture,
        TextureUpdateInfo{
            .region =
                PixelRect{
                    static_cast<Int32>(page.x),
                    static_cast<Int32>(page.y),
                    width,
                    height,
                },
            .bytesPerRow = static_cast<UIntSize>(width),
            .bytes = pixels,
        });
    if (!updated) {
      return updated.Error();
    }
    const auto inverseWidth = 1.0F / static_cast<F32>(m_impl->atlasSize.width);
    const auto inverseHeight =
        1.0F / static_cast<F32>(m_impl->atlasSize.height);
    const auto inverseScale = 1.0F / request.scaleFactor;
    auto lease = std::make_shared<GlyphAtlasLease>();
    GlyphAtlasEntry entry{
        .texture = page.texture,
        .textureCoordinates =
            Rect{
                static_cast<F32>(page.x) * inverseWidth,
                static_cast<F32>(page.y) * inverseHeight,
                static_cast<F32>(width) * inverseWidth,
                static_cast<F32>(height) * inverseHeight,
            },
        .size =
            Size{
                static_cast<F32>(width) * inverseScale,
                static_cast<F32>(height) * inverseScale,
            },
        .bearing =
            Point{
                static_cast<F32>(face->value->glyph->bitmap_left) *
                    inverseScale,
                -static_cast<F32>(face->value->glyph->bitmap_top) *
                    inverseScale,
            },
        .lease = lease,
    };
    page.x += width + 1U;
    page.rowHeight = std::max(page.rowHeight, height);
    page.usedPixelArea +=
        static_cast<UInt64>(width) * static_cast<UInt64>(height);
    page.lastUse = ++m_impl->useClock;
    page.keys.push_back(key);
    auto cachedEntry = entry;
    cachedEntry.lease.reset();
    m_impl->glyphs.emplace(
        key, Impl::GlyphRecord{
                 .entry = std::move(cachedEntry),
                 .pageIndex = pageIndex.Value(),
                 .pixelArea =
                     static_cast<UInt64>(width) *
                     static_cast<UInt64>(height),
                 .lease = lease,
             });
    ++m_impl->diagnostics.uploadCount;
    m_impl->diagnostics.entryCount = m_impl->glyphs.size();
    m_impl->diagnostics.usedPixelArea +=
        static_cast<UInt64>(width) * static_cast<UInt64>(height);
    return entry;
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to allocate glyph atlas data",
                           "ResolveGlyph");
  }
#else
  static_cast<void>(request);
  return Unavailable("ResolveGlyph");
#endif
}

auto NativeTextSystem::AtlasDiagnostics() const noexcept
    -> GlyphAtlasDiagnostics {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  auto diagnostics = m_impl->diagnostics;
  diagnostics.entryCount = m_impl->glyphs.size();
  diagnostics.pageCount = m_impl->pages.size();
  diagnostics.maximumPageCount = m_impl->maximumAtlasPages;
  diagnostics.capacityPixelArea =
      static_cast<UInt64>(m_impl->atlasSize.width) *
      static_cast<UInt64>(m_impl->atlasSize.height) *
      static_cast<UInt64>(m_impl->pages.size());
  try {
    for (const auto &[key, record] : m_impl->glyphs) {
      const auto found =
          std::find_if(diagnostics.pixelSizes.begin(),
                       diagnostics.pixelSizes.end(), [&key](const auto &size) {
                         return size.pixelSize == key.pixelSize;
                       });
      if (found != diagnostics.pixelSizes.end()) {
        ++found->entryCount;
        found->usedPixelArea += record.pixelArea;
      } else {
        diagnostics.pixelSizes.push_back(GlyphAtlasSizeDiagnostics{
            .pixelSize = key.pixelSize,
            .entryCount = 1,
            .usedPixelArea = record.pixelArea,
        });
      }
    }
    std::sort(diagnostics.pixelSizes.begin(), diagnostics.pixelSizes.end(),
              [](const auto &left, const auto &right) {
                return left.pixelSize < right.pixelSize;
              });
    diagnostics.pixelSizeCount = diagnostics.pixelSizes.size();
  } catch (...) {
    diagnostics.pixelSizes.clear();
    diagnostics.pixelSizeCount = 0;
  }
  return diagnostics;
#else
  return {};
#endif
}

auto NativeTextSystem::CoverageDiagnostics() const noexcept
    -> FontCoverageDiagnostics {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    FontCoverageDiagnostics diagnostics{
        .fallbackCodePointCount = m_impl->fallbackCodePoints.size(),
        .missingCodePointCount = m_impl->missingCodePoints.size(),
        .missingCodePoints =
            std::vector<UInt32>{m_impl->missingCodePoints.begin(),
                                m_impl->missingCodePoints.end()},
    };
    std::sort(diagnostics.missingCodePoints.begin(),
              diagnostics.missingCodePoints.end());
    diagnostics.faces.reserve(m_impl->faces.size());
    for (UInt32 index = 0; index < m_impl->faces.size(); ++index) {
      const auto &face = m_impl->faces[index];
      diagnostics.faces.push_back(FontFaceDiagnostics{
          .face = FontFaceHandle{index, 1},
          .family =
              NGIN::Text::String{face.value->family_name == nullptr
                                     ? ""
                                     : face.value->family_name},
          .style =
              NGIN::Text::String{face.value->style_name == nullptr
                                     ? ""
                                     : face.value->style_name},
          .sourcePath = face.path,
          .fallback = face.fallback,
          .resolvedCodePointCount = face.resolvedCodePoints.size(),
      });
    }
    return diagnostics;
  } catch (...) {
    return {};
  }
#else
  return {};
#endif
}

void NativeTextSystem::OnDeviceLost() noexcept {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  m_impl->DestroyPages(true);
  m_impl->renderer = nullptr;
  m_impl->NotifyResourcesInvalidated();
#endif
}

auto NativeTextSystem::OnDeviceRestored(IRenderBackend &renderer) noexcept
    -> UIResult<void> {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  try {
    m_impl->DestroyPages(true);
    m_impl->renderer = &renderer;
    auto page = m_impl->AddPage();
    if (!page) {
      return std::move(page).Error();
    }
    ++m_impl->diagnostics.restorationCount;
    m_impl->NotifyResourcesInvalidated();
    return {};
  } catch (...) {
    return NativeTextError(UIErrorCode::OutOfMemory,
                           "Unable to restore glyph atlas storage",
                           "RestoreTextDevice");
  }
#else
  static_cast<void>(renderer);
  return Unavailable("RestoreTextDevice");
#endif
}

void NativeTextSystem::SetResourcesInvalidatedCallback(
    ResourcesInvalidatedCallback callback) {
#if defined(NGIN_UI_HAS_NATIVE_TEXT)
  m_impl->invalidated = std::move(callback);
#else
  static_cast<void>(callback);
#endif
}
} // namespace NGIN::UI
