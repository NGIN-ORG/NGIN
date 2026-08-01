#include <NGIN/UI/CustomElement.hpp>

#include <NGIN/UI/DisplayList.hpp>

#include <cmath>

namespace NGIN::UI {
CustomElementContext::CustomElementContext(
    CustomStateStore &state, const ElementId identity,
    const Rect arrangedBounds, const CustomInteractionState interaction,
    const F32 scaleFactor, const MotionTransform windowTransform,
    const Detail::MotionState *motionState) noexcept
    : m_state(&state), m_identity(identity), m_arrangedBounds(arrangedBounds),
      m_interaction(interaction),
      m_scaleFactor(scaleFactor > 0.0F ? scaleFactor : 1.0F),
      m_windowTransform(windowTransform), m_motionState(motionState) {}

auto CustomElementContext::Identity() const noexcept -> ElementId {
  return m_identity;
}

auto CustomElementContext::ArrangedSize() const noexcept -> Size {
  return Size{m_arrangedBounds.width, m_arrangedBounds.height};
}

auto CustomElementContext::ToLocal(const Point windowPoint) const noexcept
    -> Point {
  return Point{
      (windowPoint.x - m_windowTransform.translation.x) /
              (std::abs(m_windowTransform.scale.x) > 0.0001F
                   ? m_windowTransform.scale.x
                   : 0.0001F) -
          m_arrangedBounds.x,
      (windowPoint.y - m_windowTransform.translation.y) /
              (std::abs(m_windowTransform.scale.y) > 0.0001F
                   ? m_windowTransform.scale.y
                   : 0.0001F) -
          m_arrangedBounds.y,
  };
}

auto CustomElementContext::Interaction() const noexcept
    -> CustomInteractionState {
  return m_interaction;
}

auto CustomElementContext::ScaleFactor() const noexcept -> F32 {
  return m_scaleFactor;
}

auto CustomElementContext::MotionValue() const noexcept -> F32 {
  return MotionValue(MotionProperty::Value);
}

auto CustomElementContext::IsMotionActive() const noexcept -> bool {
  return Detail::IsAnyMotionPropertyActive(m_motionState);
}

PaintContext::PaintContext(DisplayListBuilder &builder,
                           const Size extent) noexcept
    : m_builder(&builder), m_extent(extent) {}

auto PaintContext::Extent() const noexcept -> Size { return m_extent; }

auto PaintContext::Bounds() const noexcept -> Rect {
  return Rect{0.0F, 0.0F, m_extent.width, m_extent.height};
}

void PaintContext::Fill(const Rect rect, const Color color) {
  m_builder->Fill(rect, color);
}

void PaintContext::FillRounded(const Rect rect, const CornerRadius radius,
                               const Color color) {
  m_builder->FillRounded(rect, radius, color);
}

void PaintContext::Stroke(const Rect rect, const F32 thickness,
                          const Color color) {
  m_builder->Stroke(rect, thickness, color);
}

void PaintContext::StrokeRounded(const Rect rect, const CornerRadius radius,
                                 const F32 thickness, const Color color) {
  m_builder->StrokeRounded(rect, radius, thickness, color);
}

void PaintContext::Image(const TextureHandle texture, const Rect destination,
                         const Color tint) {
  m_builder->Image(texture, destination, tint);
}

auto ICustomElement::Arrange(CustomElementContext &, const Size)
    -> UIResult<void> {
  return {};
}

auto ICustomElement::Semantics(CustomElementContext &)
    -> UIResult<SemanticProperties> {
  return SemanticProperties{};
}

auto ICustomElement::PointerEvent(CustomElementContext &, RoutedPointerEvent &)
    -> UIResult<InvalidationKind> {
  return InvalidationKind::None;
}

auto ICustomElement::KeyEvent(CustomElementContext &, RoutedKeyEvent &)
    -> UIResult<InvalidationKind> {
  return InvalidationKind::None;
}

auto ICustomElement::TextEvent(CustomElementContext &, RoutedTextEvent &)
    -> UIResult<InvalidationKind> {
  return InvalidationKind::None;
}

auto ICustomElement::SemanticAction(CustomElementContext &,
                                    const SemanticActionRequest &)
    -> UIResult<InvalidationKind> {
  return MakeUIError(UIErrorCode::Unsupported,
                     "The custom element does not implement this semantic action",
                     "NGIN.UI", "ICustomElement::SemanticAction");
}

void ICustomElement::Unmounted(CustomElementContext &) noexcept {}
} // namespace NGIN::UI
