#include <NGIN/UI/DisplayList.hpp>

#include <algorithm>
#include <utility>

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
               DisplayListBuilder &builder) {
  const auto *node = tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  if (node->properties.paintsBackground && node->arrangedBounds.width > 0.0F &&
      node->arrangedBounds.height > 0.0F) {
    builder.Fill(node->arrangedBounds, node->properties.background);
  }
  const auto clipsChildren = node->type == ElementType::ScrollView;
  if (clipsChildren) {
    builder.PushClip(node->arrangedBounds);
  }
  for (const auto child : node->children) {
    PaintNode(tree, child, builder);
  }
  if (clipsChildren) {
    static_cast<void>(builder.PopClip());
  }
}
} // namespace

auto BuildDisplayList(const RuntimeTree &tree) -> DisplayList {
  DisplayListBuilder builder;
  PaintNode(tree, tree.Root(), builder);
  auto finished = std::move(builder).Finish();
  return finished ? std::move(finished).Value() : DisplayList{};
}
} // namespace NGIN::UI
