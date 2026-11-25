#include <NGIN/UI/SplitPane.hpp>
#include <NGIN/UI/Theme.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace NGIN::UI {
namespace {
constexpr F32 DividerWidth = 8.0F;
constexpr F32 KeyboardStep = 0.05F;

auto Fraction(const Binding<F32> &binding) -> F32 {
  const auto value = binding ? binding.Get() : 0.5F;
  return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.5F;
}

class SplitLayout final : public ILayoutPolicy {
public:
  explicit SplitLayout(Binding<F32> fraction)
      : m_fraction(std::move(fraction)) {}

  auto Measure(MeasureChildren &children, const SizeConstraints available)
      -> Size override {
    const auto width = available.maximum.width;
    const auto remaining = std::max(0.0F, width - DividerWidth);
    const auto fraction = Fraction(m_fraction);
    const auto divided = std::isfinite(width);
    const auto first =
        children.Measure(0, {.maximum = {divided ? remaining * fraction : width,
                                         available.maximum.height}});
    const auto divider = children.Measure(1, {.maximum = available.maximum});
    const auto second = children.Measure(
        2, {.maximum = {divided ? remaining * (1.0F - fraction) : width,
                        available.maximum.height}});
    return available.Constrain(
        {divided ? width : first.width + divider.width + second.width,
         std::max({first.height, second.height, divider.height})});
  }

  void Arrange(ArrangeChildren &children, const Size available) override {
    const auto divider = std::min(DividerWidth, available.width);
    const auto remaining = available.width - divider;
    const auto first = remaining * Fraction(m_fraction);
    children.Place(0, {0.0F, 0.0F, first, available.height});
    children.Place(1, {first, 0.0F, divider, available.height});
    children.Place(
        2, {first + divider, 0.0F, remaining - first, available.height});
  }

private:
  Binding<F32> m_fraction;
};

class SplitDivider final : public ICustomElement {
public:
  explicit SplitDivider(Binding<F32> fraction)
      : m_fraction(std::move(fraction)) {}

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return constraints.Constrain({DividerWidth, 0.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto palette = ColorPalette{};
    const auto state = context.Interaction();
    paint.Fill(paint.Bounds(),
               state.focused || state.hovered ? palette.focus : palette.border);
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    return SemanticProperties{
        .role = SemanticRole::Slider,
        .label = NGIN::Text::String{"Pane divider"},
        .range = SemanticRange{0.0, 1.0, Fraction(m_fraction), KeyboardStep},
        .actions = SemanticActionFlags::SetValue |
                   SemanticActionFlags::Increment |
                   SemanticActionFlags::Decrement | SemanticActionFlags::Focus};
  }

  auto PointerEvent(CustomElementContext &context, RoutedPointerEvent &event)
      -> UIResult<InvalidationKind> override {
    if (event.phase != EventPhase::Target || !context.Interaction().enabled) {
      return InvalidationKind::None;
    }
    if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
        event.button == PointerButton::Primary) {
      auto grab = context.State<Grab>("grab");
      if (!grab) {
        return std::unexpected(grab.error());
      }
      **grab = Grab{event.pointerId, context.ToLocal(event.position).x};
      event.CapturePointer();
      event.Handle();
      return InvalidationKind::Paint;
    }
    if (!event.captured) {
      return InvalidationKind::None;
    }
    if (event.eventKind == RoutedPointerEventKind::ButtonReleased &&
        event.button == PointerButton::Primary) {
      event.ReleasePointerCapture();
      event.Handle();
      return InvalidationKind::Paint;
    }
    const auto *grab = context.FindState<Grab>("grab");
    const auto available = event.parentExtent.width - DividerWidth;
    if (event.eventKind == RoutedPointerEventKind::Moved && grab &&
        grab->pointerId == event.pointerId && available > 0.0F) {
      event.Handle();
      return Set((event.parentPosition.x - grab->offset) / available);
    }
    return InvalidationKind::None;
  }

  auto KeyEvent(CustomElementContext &, RoutedKeyEvent &event)
      -> UIResult<InvalidationKind> override {
    if (event.phase != EventPhase::Target ||
        event.state == KeyState::Released) {
      return InvalidationKind::None;
    }
    F32 next = Fraction(m_fraction);
    switch (event.logicalKey) {
    case LogicalKey::Left:
      next -= KeyboardStep;
      break;
    case LogicalKey::Right:
      next += KeyboardStep;
      break;
    case LogicalKey::Home:
      next = 0.0F;
      break;
    case LogicalKey::End:
      next = 1.0F;
      break;
    default:
      return InvalidationKind::None;
    }
    event.Handle();
    return Set(next);
  }

  auto SemanticAction(CustomElementContext &,
                      const SemanticActionRequest &request)
      -> UIResult<InvalidationKind> override {
    switch (request.action) {
    case SemanticActionKind::SetValue:
      return Set(static_cast<F32>(request.numericValue));
    case SemanticActionKind::Increment:
      return Set(Fraction(m_fraction) + KeyboardStep);
    case SemanticActionKind::Decrement:
      return Set(Fraction(m_fraction) - KeyboardStep);
    default:
      return InvalidationKind::None;
    }
  }

private:
  struct Grab final {
    UInt64 pointerId{};
    F32 offset{};
  };

  auto Set(F32 value) -> UIResult<InvalidationKind> {
    if (!std::isfinite(value)) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                                         "Divider fraction must be finite",
                                         "NGIN.UI", "SplitPane"));
    }
    value = std::clamp(value, 0.0F, 1.0F);
    if (Fraction(m_fraction) == value) {
      return InvalidationKind::None;
    }
    auto result = m_fraction.Set(value);
    if (!result) {
      return std::unexpected(result.error());
    }
    return InvalidationKind::Measure | InvalidationKind::Arrange |
           InvalidationKind::Paint | InvalidationKind::Semantics;
  }

  Binding<F32> m_fraction;
};
} // namespace

void SplitPane(Composer &composer, Binding<F32> fraction,
               NGIN::Utilities::Callable<void()> first,
               NGIN::Utilities::Callable<void()> second,
               const NodeProperties &properties, const std::string_view key) {
  auto container = properties;
  container.layoutPolicy = std::make_shared<SplitLayout>(fraction);
  auto scope = composer.Begin(ElementType::Custom, container, key);
  composer.Element(ElementType::Overlay, first, "first");
  NodeProperties divider{};
  divider.interaction.enabled = properties.interaction.enabled &&
                                fraction.IsReadable() && fraction.IsWritable();
  divider.interaction.focusable = divider.interaction.enabled;
  divider.custom.onError = properties.custom.onError;
  composer.Custom(std::make_shared<SplitDivider>(std::move(fraction)), divider,
                  "divider");
  composer.Element(ElementType::Overlay, second, "second");
}
} // namespace NGIN::UI
