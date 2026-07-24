#include <NGIN/UI/DisplayList.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace NGIN::UI {
void DisplayListBuilder::PushClip(const Rect rect) {
  m_commands.emplace_back(PushClipRect{rect});
  ++m_clipDepth;
}

auto DisplayListBuilder::PopClip() noexcept -> UIResult<void> {
  if (m_clipDepth == 0) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Display-list clip stack underflow", "NGIN.UI",
                       "PopClip");
  }
  m_commands.emplace_back(NGIN::UI::PopClip{});
  --m_clipDepth;
  return {};
}

void DisplayListBuilder::PushTranslation(const F32 x, const F32 y) {
  m_commands.emplace_back(PushTransform{
      .translateX = x,
      .translateY = y,
  });
  ++m_transformDepth;
}

auto DisplayListBuilder::PopTransform() noexcept -> UIResult<void> {
  if (m_transformDepth == 0) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Display-list transform stack underflow", "NGIN.UI",
                       "PopTransform");
  }
  m_commands.emplace_back(NGIN::UI::PopTransform{});
  --m_transformDepth;
  return {};
}

void DisplayListBuilder::Fill(const Rect rect, const Color color) {
  m_commands.emplace_back(FillRect{rect, color});
}

void DisplayListBuilder::FillRounded(const Rect rect, const CornerRadius radius,
                                     const Color color) {
  m_commands.emplace_back(FillRoundedRect{rect, radius, color});
}

void DisplayListBuilder::Stroke(const Rect rect, const F32 thickness,
                                const Color color) {
  m_commands.emplace_back(StrokeRect{rect, thickness, color});
}

void DisplayListBuilder::Image(const TextureHandle texture,
                               const Rect destination, const Color tint) {
  m_commands.emplace_back(DrawImage{texture, destination, tint});
}

void DisplayListBuilder::Glyphs(const TextureHandle atlas,
                                std::vector<GlyphQuad> glyphs,
                                const Color color) {
  m_commands.emplace_back(DrawGlyphRun{atlas, std::move(glyphs), color});
}

void DisplayListBuilder::BeginOpacity(const F32 opacity) {
  m_commands.emplace_back(BeginOpacityLayer{std::clamp(opacity, 0.0F, 1.0F)});
  ++m_opacityDepth;
}

auto DisplayListBuilder::EndOpacity() noexcept -> UIResult<void> {
  if (m_opacityDepth == 0) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Display-list opacity stack underflow", "NGIN.UI",
                       "EndOpacity");
  }
  m_commands.emplace_back(EndOpacityLayer{});
  --m_opacityDepth;
  return {};
}

auto DisplayListBuilder::Finish() && noexcept -> UIResult<DisplayList> {
  if (m_clipDepth != 0 || m_transformDepth != 0 || m_opacityDepth != 0) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Display-list scopes are not balanced", "NGIN.UI",
                       "FinishDisplayList");
  }
  return std::move(m_commands);
}

auto DisplayListBuilder::Commands() const noexcept -> const DisplayList & {
  return m_commands;
}

namespace {
void PaintNode(const RuntimeTree &tree, const ElementHandle handle,
               DisplayListBuilder &builder,
               const ElementHandle popupRoot = {}) {
  const auto *node = tree.Get(handle);
  if (node == nullptr ||
      (node->type == ElementType::Popup && handle != popupRoot)) {
    return;
  }
  if (node->properties.paintsBackground && node->arrangedBounds.width > 0.0F &&
      node->arrangedBounds.height > 0.0F) {
    builder.Fill(node->arrangedBounds, node->properties.background);
  }
  const auto clipsText =
      node->type == ElementType::Text && node->properties.text.clip;
  if (clipsText) {
    builder.PushClip(node->arrangedBounds);
  }
  if (node->type == ElementType::Text && node->text.valid) {
    const auto originX =
        node->arrangedBounds.x + node->properties.layout.padding.left;
    const auto originY =
        node->arrangedBounds.y + node->properties.layout.padding.top;
    for (const auto &run : node->text.glyphRuns) {
      auto glyphs = run.glyphs;
      for (auto &glyph : glyphs) {
        glyph.destination.x += originX;
        glyph.destination.y += originY;
      }
      builder.Glyphs(run.texture, std::move(glyphs),
                     node->properties.text.color);
    }
  }
  if (clipsText) {
    static_cast<void>(builder.PopClip());
  }
  const auto clipsChildren = node->type == ElementType::ScrollView;
  if (clipsChildren) {
    builder.PushClip(node->arrangedBounds);
  }
  for (const auto child : node->children) {
    PaintNode(tree, child, builder, popupRoot);
  }
  if (clipsChildren) {
    static_cast<void>(builder.PopClip());
  }
}

void CollectPopups(const RuntimeTree &tree, const ElementHandle handle,
                   std::vector<ElementHandle> &popups) {
  const auto *node = tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  if (node->type == ElementType::Popup) {
    popups.push_back(handle);
  }
  for (const auto child : node->children) {
    CollectPopups(tree, child, popups);
  }
}
} // namespace

auto BuildDisplayList(const RuntimeTree &tree) -> DisplayList {
  DisplayListBuilder builder;
  PaintNode(tree, tree.Root(), builder);
  std::vector<ElementHandle> popups;
  CollectPopups(tree, tree.Root(), popups);
  for (const auto popup : popups) {
    PaintNode(tree, popup, builder, popup);
  }
  auto finished = std::move(builder).Finish();
  return finished ? std::move(finished).Value() : DisplayList{};
}
} // namespace NGIN::UI
