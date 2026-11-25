#pragma once

#include <NGIN/UI/Components.hpp>

namespace NGIN::UI::Testing {
struct HeadlessText final : ITextLayout,
                            ITextGeometry,
                            IGlyphAtlas,
                            IGraphemeSegmenter {
  auto LayoutParagraph(const ParagraphRequest &) noexcept
      -> UIResult<ParagraphLayout> override {
    return ParagraphLayout{};
  }
  auto CaretRect(const ParagraphLayout &, NGIN::UIntSize) noexcept
      -> UIResult<Rect> override {
    return Rect{};
  }
  auto RangeRects(const ParagraphLayout &, NGIN::UIntSize,
                  NGIN::UIntSize) noexcept
      -> UIResult<std::vector<Rect>> override {
    return std::vector<Rect>{};
  }
  auto ResolveGlyph(const GlyphAtlasRequest &) noexcept
      -> UIResult<GlyphAtlasEntry> override {
    return GlyphAtlasEntry{};
  }
  auto Segment(const NGIN::Text::String &) noexcept
      -> UIResult<std::vector<GraphemeCluster>> override {
    return std::vector<GraphemeCluster>{};
  }
  auto Environment() -> Views::ViewEnvironment {
    return {*this, *this, *this, *this};
  }
};
} // namespace NGIN::UI::Testing
