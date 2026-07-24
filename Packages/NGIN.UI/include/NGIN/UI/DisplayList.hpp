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
struct PushClipRect final {
  Rect rect{};
};

struct PopClip final {};

struct PushTransform final {
  F32 translateX{0.0F};
  F32 translateY{0.0F};
  F32 scaleX{1.0F};
  F32 scaleY{1.0F};
};

struct PopTransform final {};

struct FillRect final {
  Rect rect{};
  Color color{};
};

struct FillRoundedRect final {
  Rect rect{};
  CornerRadius radius{};
  Color color{};
};

struct StrokeRect final {
  Rect rect{};
  F32 thickness{1.0F};
  Color color{};
};

struct DrawImage final {
  TextureHandle texture{};
  Rect destination{};
  Color tint{1.0F, 1.0F, 1.0F, 1.0F};
};

struct DrawGlyphRun final {
  TextureHandle atlas{};
  std::vector<GlyphQuad> glyphs{};
  Color color{};
};

struct BeginOpacityLayer final {
  F32 opacity{1.0F};
};

struct EndOpacityLayer final {};

using DisplayCommand =
    std::variant<PushClipRect, PopClip, PushTransform, PopTransform, FillRect,
                 FillRoundedRect, StrokeRect, DrawImage, DrawGlyphRun,
                 BeginOpacityLayer, EndOpacityLayer>;

using DisplayList = std::vector<DisplayCommand>;

class DisplayListBuilder final {
public:
  void PushClip(Rect rect);
  auto PopClip() noexcept -> UIResult<void>;
  void PushTranslation(F32 x, F32 y);
  auto PopTransform() noexcept -> UIResult<void>;
  void Fill(Rect rect, Color color);
  void FillRounded(Rect rect, CornerRadius radius, Color color);
  void Stroke(Rect rect, F32 thickness, Color color);
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
