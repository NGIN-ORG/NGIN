#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/RuntimeTree.hpp>
#include <NGIN/UI/Style.hpp>

#include <variant>
#include <vector>

namespace NGIN::UI {
/// @brief Display command that pushes a rectangular clipping region.
struct PushClipRect final {
  Rect rect{};
};

/// @brief Display command that restores the preceding clipping region.
struct PopClip final {};

/// @brief Display command that pushes a translation transform.
struct PushTransform final {
  F32 translateX{0.0F};
  F32 translateY{0.0F};
  F32 scaleX{1.0F};
  F32 scaleY{1.0F};
};

/// @brief Display command that restores the preceding transform.
struct PopTransform final {};

/// @brief Display command that fills an axis-aligned rectangle.
struct FillRect final {
  Rect rect{};
  Color color{};
};

/// @brief Display command that fills a rounded rectangle.
struct FillRoundedRect final {
  Rect rect{};
  CornerRadius radius{};
  Color color{};
};

/// @brief Display command that outlines an axis-aligned rectangle.
struct StrokeRect final {
  Rect rect{};
  F32 thickness{1.0F};
  Color color{};
};

/// @brief Display command that outlines a rounded rectangle.
struct StrokeRoundedRect final {
  Rect rect{};
  CornerRadius radius{};
  F32 thickness{1.0F};
  Color color{};
};

/// @brief Display command that draws a textured image region.
struct DrawImage final {
  TextureHandle texture{};
  Rect destination{};
  Color tint{1.0F, 1.0F, 1.0F, 1.0F};
};

/// @brief Display command that draws indexed glyph geometry.
struct DrawGlyphRun final {
  TextureHandle atlas{};
  std::vector<GlyphQuad> glyphs{};
  Color color{};
};

/// @brief Display command that begins a composited opacity layer.
struct BeginOpacityLayer final {
  F32 opacity{1.0F};
};

/// @brief Display command that ends the current opacity layer.
struct EndOpacityLayer final {};

/// @brief Variant containing every backend-neutral display command.
using DisplayCommand =
    std::variant<PushClipRect, PopClip, PushTransform, PopTransform, FillRect,
                 FillRoundedRect, StrokeRect, StrokeRoundedRect, DrawImage,
                 DrawGlyphRun, BeginOpacityLayer, EndOpacityLayer>;

/// @brief Ordered sequence of commands produced during painting.
using DisplayList = std::vector<DisplayCommand>;

/// @brief Validates and records display commands in painting order.
class DisplayListBuilder final {
public:
  void PushClip(Rect rect);
  auto PopClip() noexcept -> UIResult<void>;
  void PushTranslation(F32 x, F32 y);
  auto PopTransform() noexcept -> UIResult<void>;
  void Fill(Rect rect, Color color);
  void FillRounded(Rect rect, CornerRadius radius, Color color);
  void Stroke(Rect rect, F32 thickness, Color color);
  void StrokeRounded(Rect rect, CornerRadius radius, F32 thickness,
                     Color color);
  void Image(TextureHandle texture, Rect destination,
             Color tint = Color{1.0F, 1.0F, 1.0F, 1.0F});
  void Glyphs(TextureHandle atlas, std::vector<GlyphQuad> glyphs, Color color);
  void BeginOpacity(F32 opacity);
  auto EndOpacity() noexcept -> UIResult<void>;

  [[nodiscard]] auto Finish() && noexcept -> UIResult<DisplayList>;
  [[nodiscard]] auto Commands() const noexcept -> const DisplayList &;

private:
  DisplayList m_commands{};
  UIntSize m_clipDepth{0};
  UIntSize m_transformDepth{0};
  UIntSize m_opacityDepth{0};
};

[[nodiscard]] auto BuildDisplayList(const RuntimeTree &tree) -> DisplayList;
} // namespace NGIN::UI
