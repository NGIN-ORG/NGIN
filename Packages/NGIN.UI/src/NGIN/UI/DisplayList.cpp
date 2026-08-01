#include <NGIN/UI/DisplayList.hpp>

#include "MotionInternal.hpp"

#include "ScrollBarGeometry.hpp"

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
  PushTransform(x, y);
}

void DisplayListBuilder::PushTransform(const F32 translateX,
                                       const F32 translateY,
                                       const F32 scaleX, const F32 scaleY) {
  m_commands.emplace_back(NGIN::UI::PushTransform{
      .translateX = translateX,
      .translateY = translateY,
      .scaleX = scaleX,
      .scaleY = scaleY,
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

void DisplayListBuilder::StrokeRounded(const Rect rect,
                                       const CornerRadius radius,
                                       const F32 thickness, const Color color) {
  m_commands.emplace_back(StrokeRoundedRect{rect, radius, thickness, color});
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
[[nodiscard]] auto HasRadius(const CornerRadius radius) noexcept -> bool {
  return radius.topLeft > 0.0F || radius.topRight > 0.0F ||
         radius.bottomRight > 0.0F || radius.bottomLeft > 0.0F;
}

[[nodiscard]] auto IsUniform(const Thickness thickness) noexcept -> bool {
  return thickness.left == thickness.top && thickness.left == thickness.right &&
         thickness.left == thickness.bottom;
}

[[nodiscard]] auto VisualStateFor(const RuntimeNode &node) noexcept
    -> VisualStateFlags {
  auto state = node.properties.visual.state;
  if (node.interaction.hovered) {
    state |= VisualStateFlags::Hovered;
  }
  if (node.interaction.pressed || node.interaction.keyboardPressed) {
    state |= VisualStateFlags::Pressed;
  }
  if (node.interaction.focused) {
    state |= VisualStateFlags::Focused;
  }
  if (!node.properties.interaction.enabled) {
    state |= VisualStateFlags::Disabled;
  }
  if ((node.type == ElementType::TextField ||
       node.type == ElementType::TextArea) &&
      node.properties.textField.readOnly) {
    state |= VisualStateFlags::ReadOnly;
  }
  return state;
}

[[nodiscard]] auto CustomContextFor(const RuntimeNode &node)
    -> CustomElementContext {
  const auto motion = Detail::SnapshotFor(node);
  return CustomElementContext{
      *node.custom.state,
      node.id,
      node.arrangedBounds,
      CustomInteractionState{
          .hovered = node.interaction.hovered,
          .pressed =
              node.interaction.pressed || node.interaction.keyboardPressed,
          .focused = node.interaction.focused,
          .enabled = node.properties.interaction.enabled,
      },
      node.custom.scaleFactor,
      Detail::TransformFor(node),
      motion.value,
      motion.active,
  };
}

void ReportCustomError(const RuntimeNode &node, const UIError &error) noexcept {
  if (!node.properties.custom.onError) {
    return;
  }
  try {
    node.properties.custom.onError(error);
  } catch (...) {
  }
}

void PaintCustom(const RuntimeNode &node, DisplayListBuilder &builder) {
  if (node.type != ElementType::CustomElement || !node.custom.state ||
      !node.properties.custom.element || node.arrangedBounds.width <= 0.0F ||
      node.arrangedBounds.height <= 0.0F) {
    return;
  }

  builder.PushClip(node.arrangedBounds);
  builder.PushTranslation(node.arrangedBounds.x, node.arrangedBounds.y);
  try {
    auto context = CustomContextFor(node);
    PaintContext paint{
        builder,
        Size{node.arrangedBounds.width, node.arrangedBounds.height},
    };
    auto painted = node.properties.custom.element->Paint(context, paint);
    if (!painted) {
      ReportCustomError(node, painted.Error());
    }
  } catch (const std::bad_alloc &) {
    ReportCustomError(node, MakeUIError(UIErrorCode::OutOfMemory,
                                        "Custom painting allocation failed",
                                        "NGIN.UI", "ICustomElement::Paint"));
  } catch (...) {
    ReportCustomError(node,
                      MakeUIError(UIErrorCode::InvalidState,
                                  "Custom painting callback threw an exception",
                                  "NGIN.UI", "ICustomElement::Paint"));
  }
  static_cast<void>(builder.PopTransform());
  static_cast<void>(builder.PopClip());
}

void PaintBorder(DisplayListBuilder &builder, const Rect bounds,
                 const VisualStyle &style) {
  if (!style.borderColor) {
    return;
  }
  const auto thickness = style.borderThickness;
  const auto maximumThickness = std::min(bounds.width, bounds.height) * 0.5F;
  const auto left = std::clamp(thickness.left, 0.0F, maximumThickness);
  const auto top = std::clamp(thickness.top, 0.0F, maximumThickness);
  const auto right = std::clamp(thickness.right, 0.0F, maximumThickness);
  const auto bottom = std::clamp(thickness.bottom, 0.0F, maximumThickness);
  if (left <= 0.0F && top <= 0.0F && right <= 0.0F && bottom <= 0.0F) {
    return;
  }

  if (IsUniform(Thickness{left, top, right, bottom})) {
    if (HasRadius(style.cornerRadius)) {
      builder.StrokeRounded(bounds, style.cornerRadius, left,
                            *style.borderColor);
    } else {
      builder.Stroke(bounds, left, *style.borderColor);
    }
    return;
  }

  if (top > 0.0F) {
    builder.Fill(Rect{bounds.x, bounds.y, bounds.width, top},
                 *style.borderColor);
  }
  if (bottom > 0.0F) {
    builder.Fill(
        Rect{bounds.x, bounds.y + bounds.height - bottom, bounds.width, bottom},
        *style.borderColor);
  }
  const auto sideHeight = std::max(0.0F, bounds.height - top - bottom);
  if (left > 0.0F && sideHeight > 0.0F) {
    builder.Fill(Rect{bounds.x, bounds.y + top, left, sideHeight},
                 *style.borderColor);
  }
  if (right > 0.0F && sideHeight > 0.0F) {
    builder.Fill(Rect{bounds.x + bounds.width - right, bounds.y + top, right,
                      sideHeight},
                 *style.borderColor);
  }
}

[[nodiscard]] auto PaintVisual(const RuntimeNode &node,
                               DisplayListBuilder &builder) -> VisualStyle {
  const auto state = VisualStateFor(node);
  auto style = ResolveVisualStyle(node.properties.visual, state);
  if (!style.background && node.properties.paintsBackground) {
    style.background = node.properties.background;
  }
  const auto motion = Detail::SnapshotFor(node);
  if (node.motion && motion.hasBackground) {
    style.background = motion.background;
  }
  if (node.motion && motion.hasForeground) {
    style.foreground = motion.foreground;
  }
  if (node.motion && motion.hasBorderColor) {
    style.borderColor = motion.borderColor;
  }

  const auto bounds = node.arrangedBounds;
  const auto hasBounds = bounds.width > 0.0F && bounds.height > 0.0F;
  if (!hasBounds) {
    return style;
  }

  const auto &focus = node.properties.visual.focus;
  const auto focusOpacity = node.motion
                                ? motion.focusOpacity
                                : (HasVisualState(state,
                                                  VisualStateFlags::Focused)
                                       ? 1.0F
                                       : 0.0F);
  if (focusOpacity > 0.0F && focus.enabled && focus.color &&
      focus.thickness > 0.0F) {
    auto focusColor = *focus.color;
    focusColor.alpha *= focusOpacity;
    const auto offset = std::max(0.0F, focus.offset);
    const Rect focusBounds{
        bounds.x - offset,
        bounds.y - offset,
        bounds.width + offset * 2.0F,
        bounds.height + offset * 2.0F,
    };
    if (HasRadius(focus.cornerRadius)) {
      builder.StrokeRounded(focusBounds, focus.cornerRadius, focus.thickness,
                            focusColor);
    } else {
      builder.Stroke(focusBounds, focus.thickness, focusColor);
    }
  }

  if (style.background) {
    if (HasRadius(style.cornerRadius)) {
      builder.FillRounded(bounds, style.cornerRadius, *style.background);
    } else {
      builder.Fill(bounds, *style.background);
    }
  }
  PaintBorder(builder, bounds, style);
  return style;
}

void PaintNode(const RuntimeTree &tree, const ElementHandle handle,
               DisplayListBuilder &builder,
               const ElementHandle popupRoot = {}) {
  const auto *node = tree.Get(handle);
  if (node == nullptr ||
      node->properties.visibility != ElementVisibility::Visible ||
      (node->type == ElementType::Popup && handle != popupRoot)) {
    return;
  }
  const auto motion = Detail::SnapshotFor(*node);
  const auto transform = Detail::TransformFor(*node);
  const auto hasTransform = node->motion &&
                            (transform.translation != Point{} ||
                             transform.scale != Point{1.0F, 1.0F});
  const auto hasOpacity = node->motion && motion.opacity < 0.9999F;
  if (hasTransform) {
    builder.PushTransform(transform.translation.x, transform.translation.y,
                          transform.scale.x, transform.scale.y);
  }
  if (hasOpacity) {
    builder.BeginOpacity(motion.opacity);
  }
  const auto visual = PaintVisual(*node, builder);
  const auto paintsText = (node->type == ElementType::Text ||
                           node->type == ElementType::TextField ||
                           node->type == ElementType::TextArea) &&
                          node->text.valid;
  const auto clipsText = paintsText && node->properties.text.clip;
  if (clipsText) {
    builder.PushClip(node->arrangedBounds);
  }
  if (paintsText) {
    const auto originX =
        node->arrangedBounds.x + node->properties.layout.padding.left -
        (node->type == ElementType::TextArea ? node->scroll.offset.x : 0.0F);
    const auto originY =
        node->arrangedBounds.y + node->properties.layout.padding.top -
        (node->type == ElementType::TextArea ? node->scroll.offset.y : 0.0F);
    if (node->type == ElementType::TextField ||
        node->type == ElementType::TextArea) {
      for (auto selection : node->text.selectionRects) {
        selection.x += originX;
        selection.y += originY;
        builder.Fill(selection, node->properties.textField.selectionColor);
      }
    }
    for (const auto &run : node->text.glyphRuns) {
      auto glyphs = run.glyphs;
      for (auto &glyph : glyphs) {
        glyph.destination.x += originX;
        glyph.destination.y += originY;
      }
      builder.Glyphs(run.texture, std::move(glyphs),
                     visual.foreground.value_or(node->properties.text.color));
    }
    if (node->type == ElementType::TextField ||
        node->type == ElementType::TextArea) {
      for (auto composition : node->text.compositionRects) {
        composition.x += originX;
        composition.y += originY + composition.height -
                         std::max(1.0F, node->properties.textField.caretWidth);
        composition.height =
            std::max(1.0F, node->properties.textField.caretWidth);
        builder.Fill(composition, node->properties.textField.compositionColor);
      }
      if (node->interaction.focused && node->text.hasCaret) {
        auto caret = node->text.caretRect;
        caret.x += originX;
        caret.y += originY;
        caret.width = std::max(1.0F, node->properties.textField.caretWidth);
        builder.Fill(caret, node->properties.textField.caretColor);
      }
    }
  }
  if (clipsText) {
    static_cast<void>(builder.PopClip());
  }
  if (node->type == ElementType::Image && node->image.valid) {
    if (node->properties.image.clip) {
      builder.PushClip(node->arrangedBounds);
    }
    builder.Image(node->image.texture, node->image.destination,
                  node->properties.image.tint);
    if (node->properties.image.clip) {
      static_cast<void>(builder.PopClip());
    }
  }
  PaintCustom(*node, builder);
  const auto clipsChildren = node->type == ElementType::ScrollView ||
                             node->type == ElementType::ListView ||
                             (node->type == ElementType::Canvas &&
                              node->properties.canvas.clipToBounds);
  if (clipsChildren) {
    builder.PushClip(node->arrangedBounds);
  }
  for (const auto child : node->children) {
    PaintNode(tree, child, builder, popupRoot);
  }
  if (clipsChildren) {
    static_cast<void>(builder.PopClip());
  }
  if (node->type == ElementType::ScrollView ||
      node->type == ElementType::ListView ||
      node->type == ElementType::TextArea) {
    const auto bars = Detail::ComputeScrollBars(
        node->arrangedBounds, node->properties.scroll, node->scroll);
    const auto thumbColor = node->interaction.hovered
                                ? node->properties.scroll.scrollbarThumbHovered
                                : node->properties.scroll.scrollbarThumb;
    if (bars.hasHorizontal) {
      builder.FillRounded(
          bars.horizontalTrack,
          CornerRadius::Uniform(Dp{bars.horizontalTrack.height * 0.5F}),
          node->properties.scroll.scrollbarTrack);
      builder.FillRounded(
          bars.horizontalThumb,
          CornerRadius::Uniform(Dp{bars.horizontalThumb.height * 0.5F}),
          thumbColor);
    }
    if (bars.hasVertical) {
      builder.FillRounded(
          bars.verticalTrack,
          CornerRadius::Uniform(Dp{bars.verticalTrack.width * 0.5F}),
          node->properties.scroll.scrollbarTrack);
      builder.FillRounded(
          bars.verticalThumb,
          CornerRadius::Uniform(Dp{bars.verticalThumb.width * 0.5F}),
          thumbColor);
    }
  }
  if (hasOpacity) {
    static_cast<void>(builder.EndOpacity());
  }
  if (hasTransform) {
    static_cast<void>(builder.PopTransform());
  }
}

void CollectPopups(const RuntimeTree &tree, const ElementHandle handle,
                   std::vector<ElementHandle> &popups) {
  const auto *node = tree.Get(handle);
  if (node == nullptr ||
      node->properties.visibility != ElementVisibility::Visible) {
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
