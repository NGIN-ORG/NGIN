#include <NGIN/UIGallery/CustomControls.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace NGIN::UIGallery {
namespace {
using NGIN::F32;
using NGIN::Text::String;
using namespace NGIN::UI;

constexpr auto NoSelection = std::numeric_limits<NGIN::UIntSize>::max();

[[nodiscard]] auto Percentage(const F32 value) -> String {
  const auto number =
      std::to_string(static_cast<std::uint32_t>(std::round(value * 100.0F)));
  String result{number.c_str()};
  result.Append("%");
  return result;
}

[[nodiscard]] auto ExampleProperties() -> NodeProperties {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  return properties;
}

void ComposeCenteredLabel(Composer &composer, NativeTextSystem &text,
                          const Theme &theme, const char *label,
                          const Size size, const std::string_view key) {
  NodeProperties properties{};
  properties.layout.preferredSize = size;
  properties.layout.horizontalAlignment = HorizontalAlignment::Center;
  properties.layout.verticalAlignment = VerticalAlignment::Center;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = theme.typography.caption;
  properties.text.color = theme.colors.accentForeground;
  properties.text.geometry = &text;
  properties.semantics.hidden = true;
  composer.Text(String{label}, text, text, properties, key);
}
} // namespace

BadgeElement::BadgeElement(String label, const Color background,
                           const Color foreground)
    : m_label(std::move(label)), m_background(background),
      m_foreground(foreground) {}

auto BadgeElement::Measure(CustomElementContext &,
                           const SizeConstraints constraints)
    -> UIResult<Size> {
  return constraints.Constrain(Size{132.0F, 42.0F});
}

auto BadgeElement::Paint(CustomElementContext &, PaintContext &paint)
    -> UIResult<void> {
  paint.FillRounded(paint.Bounds(), CornerRadius::Uniform(Dp{21.0F}),
                    m_background);
  paint.FillRounded(Rect{12.0F, 15.0F, 12.0F, 12.0F},
                    CornerRadius::Uniform(Dp{6.0F}), m_foreground);
  return {};
}

auto BadgeElement::Semantics(CustomElementContext &)
    -> UIResult<SemanticProperties> {
  return SemanticProperties{
      .role = SemanticRole::Text,
      .label = m_label,
      .description = String{"Custom-painted status badge"},
  };
}

ProgressRingElement::ProgressRingElement(const F32 progress, const Color track,
                                         const Color value)
    : m_progress(std::clamp(progress, 0.0F, 1.0F)), m_track(track),
      m_value(value) {}

auto ProgressRingElement::Measure(CustomElementContext &,
                                  const SizeConstraints constraints)
    -> UIResult<Size> {
  return constraints.Constrain(Size{76.0F, 76.0F});
}

auto ProgressRingElement::Paint(CustomElementContext &, PaintContext &paint)
    -> UIResult<void> {
  constexpr NGIN::UIntSize SegmentCount = 16;
  const auto extent = paint.Extent();
  const auto center = Point{extent.width * 0.5F, extent.height * 0.5F};
  const auto radius =
      std::max(0.0F, std::min(extent.width, extent.height) * 0.5F - 8.0F);
  const auto active =
      static_cast<NGIN::UIntSize>(std::round(m_progress * SegmentCount));
  constexpr auto Tau = 6.28318530717958647692F;
  for (NGIN::UIntSize index = 0; index < SegmentCount; ++index) {
    const auto angle =
        -Tau * 0.25F + Tau * static_cast<F32>(index) / SegmentCount;
    const auto x = center.x + std::cos(angle) * radius - 3.5F;
    const auto y = center.y + std::sin(angle) * radius - 3.5F;
    paint.FillRounded(Rect{x, y, 7.0F, 7.0F}, CornerRadius::Uniform(Dp{3.5F}),
                      index < active ? m_value : m_track);
  }
  return {};
}

auto ProgressRingElement::Semantics(CustomElementContext &)
    -> UIResult<SemanticProperties> {
  return SemanticProperties{
      .role = SemanticRole::Image,
      .label = String{"Custom progress ring"},
      .value = Percentage(m_progress),
  };
}

BarChartElement::BarChartElement(std::vector<F32> values, const Color track,
                                 const Color value, const Color selected)
    : m_values(std::move(values)), m_track(track), m_value(value),
      m_selected(selected) {
  for (auto &entry : m_values) {
    entry = std::clamp(entry, 0.0F, 1.0F);
  }
}

auto BarChartElement::Measure(CustomElementContext &,
                              const SizeConstraints constraints)
    -> UIResult<Size> {
  return constraints.Constrain(Size{300.0F, 116.0F});
}

auto BarChartElement::Paint(CustomElementContext &context, PaintContext &paint)
    -> UIResult<void> {
  const auto bounds = paint.Bounds();
  paint.FillRounded(bounds, CornerRadius::Uniform(Dp{8.0F}), m_track);
  if (m_values.empty()) {
    return {};
  }

  const auto *selection = context.FindState<NGIN::UIntSize>("selected-bar");
  const auto selected = selection != nullptr ? *selection : NoSelection;
  constexpr auto padding = 12.0F;
  constexpr auto gap = 7.0F;
  const auto availableWidth =
      std::max(0.0F, bounds.width - padding * 2.0F -
                         gap * static_cast<F32>(m_values.size() - 1));
  const auto barWidth = availableWidth / static_cast<F32>(m_values.size());
  const auto chartHeight = std::max(0.0F, bounds.height - padding * 2.0F);
  for (NGIN::UIntSize index = 0; index < m_values.size(); ++index) {
    const auto height = chartHeight * m_values[index];
    const auto x = padding + static_cast<F32>(index) * (barWidth + gap);
    const auto y = bounds.height - padding - height;
    paint.FillRounded(Rect{x, y, barWidth, height},
                      CornerRadius::Uniform(Dp{3.0F}),
                      index == selected ? m_selected : m_value);
  }
  return {};
}

auto BarChartElement::Semantics(CustomElementContext &context)
    -> UIResult<SemanticProperties> {
  String value{"No bar selected"};
  if (const auto *selection = context.FindState<NGIN::UIntSize>("selected-bar");
      selection != nullptr && *selection < m_values.size()) {
    const auto number = std::to_string(*selection + 1);
    value = String{"Bar "};
    value.Append(number.c_str());
    value.Append(": ");
    value.Append(Percentage(m_values[*selection]));
  }
  return SemanticProperties{
      .role = SemanticRole::Image,
      .label = String{"Interactive custom bar chart"},
      .value = std::move(value),
      .description = String{"Select a bar with the pointer"},
      .actions = SemanticActionFlags::Focus | SemanticActionFlags::SetValue,
  };
}

auto BarChartElement::PointerEvent(CustomElementContext &context,
                                   RoutedPointerEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target ||
      event.eventKind != RoutedPointerEventKind::ButtonPressed ||
      m_values.empty()) {
    return InvalidationKind::None;
  }

  const auto local = context.ToLocal(event.position);
  const auto width = std::max(1.0F, context.ArrangedSize().width);
  const auto normalized = std::clamp(local.x / width, 0.0F, 0.9999F);
  const auto selected =
      std::min(static_cast<NGIN::UIntSize>(normalized *
                                           static_cast<F32>(m_values.size())),
               m_values.size() - 1);
  auto state = context.State<NGIN::UIntSize>("selected-bar", NoSelection);
  if (!state) {
    return state.Error();
  }
  *state.Value() = selected;
  event.Handle();
  return InvalidationKind::Paint | InvalidationKind::Semantics;
}

MotionDialElement::MotionDialElement(const Color track, const Color value)
    : m_track(track), m_value(value) {}

auto MotionDialElement::Measure(CustomElementContext &,
                                const SizeConstraints constraints)
    -> UIResult<Size> {
  return constraints.Constrain(Size{150.0F, 96.0F});
}

auto MotionDialElement::Paint(CustomElementContext &context,
                              PaintContext &paint) -> UIResult<void> {
  const auto bounds = paint.Bounds();
  paint.FillRounded(bounds, CornerRadius::Uniform(Dp{10.0F}), m_track);
  const auto sweep = context.MotionValue(MotionDialSweep);
  constexpr auto Pi = 3.14159265358979323846F;
  const auto angle = (-0.75F + sweep * 1.5F) * Pi;
  const auto center = Point{bounds.width * 0.5F, bounds.height * 0.62F};
  const auto radius = std::min(bounds.width, bounds.height) * 0.32F;
  const auto endpoint = Point{center.x + std::cos(angle) * radius,
                              center.y + std::sin(angle) * radius};
  constexpr NGIN::UIntSize SegmentCount = 16;
  for (NGIN::UIntSize index = 0; index <= SegmentCount; ++index) {
    const auto progress = static_cast<F32>(index) / SegmentCount;
    const auto tickAngle = (-0.75F + progress * 1.5F) * Pi;
    paint.FillRounded(
        Rect{center.x + std::cos(tickAngle) * radius - 2.0F,
             center.y + std::sin(tickAngle) * radius - 2.0F, 4.0F, 4.0F},
        CornerRadius::Uniform(Dp{2.0F}), m_value);
  }
  const auto delta = Point{endpoint.x - center.x, endpoint.y - center.y};
  const auto steps = std::max(1, static_cast<int>(std::ceil(radius)));
  for (int index = 0; index <= steps; ++index) {
    const auto progress = static_cast<F32>(index) / static_cast<F32>(steps);
    paint.FillRounded(
        Rect{center.x + delta.x * progress - 2.5F,
             center.y + delta.y * progress - 2.5F, 5.0F, 5.0F},
        CornerRadius::Uniform(Dp{2.5F}), m_value);
  }
  paint.FillRounded(Rect{center.x - 6.0F, center.y - 6.0F, 12.0F, 12.0F},
                    CornerRadius::Uniform(Dp{6.0F}), m_value);
  return {};
}

auto MotionDialElement::Semantics(CustomElementContext &context)
    -> UIResult<SemanticProperties> {
  return SemanticProperties{
      .role = SemanticRole::ProgressBar,
      .label = String{"Animated custom dial"},
      .value = Percentage(context.MotionValue(MotionDialSweep)),
      .description = String{"The dial reads a custom animation property"},
  };
}

void ComposeCustomControlExamples(Composer &composer, NativeTextSystem &text,
                                  const Theme &theme) {
  NodeProperties row{};
  row.layout.gap = theme.spacing.spacious;
  row.layout.horizontalAlignment = HorizontalAlignment::Start;
  row.layout.verticalAlignment = VerticalAlignment::Center;
  composer.Row(
      [&] {
        NodeProperties badgeOverlay{};
        badgeOverlay.layout.preferredSize = Size{132.0F, 42.0F};
        badgeOverlay.layout.horizontalAlignment = HorizontalAlignment::Start;
        badgeOverlay.layout.verticalAlignment = VerticalAlignment::Center;
        composer.Element(
            ElementType::Overlay, badgeOverlay,
            [&] {
              composer.Custom(
                  std::make_shared<BadgeElement>(String{"Stable custom badge"},
                                                 theme.colors.accent,
                                                 theme.colors.accentForeground),
                  ExampleProperties(), "badge-paint");
              ComposeCenteredLabel(composer, text, theme, "  STABLE",
                                   Size{132.0F, 42.0F}, "badge-label");
            },
            "badge");

        composer.Custom(std::make_shared<ProgressRingElement>(
                            0.68F, theme.colors.border, theme.colors.focus),
                        ExampleProperties(), "progress-ring");

        auto chartProperties = ExampleProperties();
        chartProperties.interaction.focusable = true;
        composer.Custom(
            std::make_shared<BarChartElement>(
                std::vector<F32>{0.28F, 0.72F, 0.46F, 0.92F, 0.61F, 0.84F},
                theme.colors.raisedSurface, theme.colors.accent,
                theme.colors.focus),
            chartProperties, "bar-chart");
      },
      "custom-examples");
}
} // namespace NGIN::UIGallery
