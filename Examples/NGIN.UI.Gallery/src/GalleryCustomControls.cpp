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
constexpr auto CustomControlInvalidation =
    InvalidationKind::Paint | InvalidationKind::Semantics;

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
                          const Size size, const Color color,
                          const std::string_view key) {
  NodeProperties properties{};
  properties.layout.preferredSize = size;
  properties.layout.horizontalAlignment = HorizontalAlignment::Center;
  properties.layout.verticalAlignment = VerticalAlignment::Center;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = theme.typography.caption;
  properties.text.color = color;
  properties.text.geometry = &text;
  properties.text.alignment = TextAlignment::Center;
  properties.semantics.hidden = true;
  composer.Text(String{label}, text, text, properties, key);
}

void ComposeTextBlock(Composer &composer, NativeTextSystem &text, String value,
                      const F32 size, const Color color,
                      const std::string_view key,
                      const SemanticRole role = SemanticRole::Text) {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = size;
  properties.text.lineHeight = size + 6.0F;
  properties.text.color = color;
  properties.text.geometry = &text;
  properties.text.wrapping = TextWrapping::Wrap;
  properties.semantics.role = role;
  composer.Text(std::move(value), text, text, properties, key);
}

template <typename ComposeDemo>
void ComposeDemoSurface(Composer &composer, NativeTextSystem &text,
                        const Theme &theme, const char *title,
                        const char *description, const F32 width,
                        const std::string_view key, ComposeDemo &&composeDemo) {
  NodeProperties surface{};
  surface.layout.preferredSize = Size{width, 220.0F};
  surface.layout.maximumSize.width = width;
  surface.layout.padding = Thickness::Uniform(Dp{16.0F});
  surface.layout.gap = theme.spacing.regular;
  surface.layout.horizontalAlignment = HorizontalAlignment::Start;
  surface.layout.verticalAlignment = VerticalAlignment::Start;
  surface.visual = MakePanelVisual(theme);
  surface.visual.base.background = theme.colors.sunkenSurface;
  surface.semantics.role = SemanticRole::Group;
  surface.semantics.label = String{title};
  composer.Element(
      ElementType::Column, surface,
      [&] {
        ComposeTextBlock(composer, text, String{title}, 16.0F,
                         theme.colors.foreground, "title",
                         SemanticRole::Heading);
        ComposeTextBlock(composer, text, String{description},
                         theme.typography.caption, theme.colors.mutedForeground,
                         "description");
        std::forward<ComposeDemo>(composeDemo)();
      },
      key);
}

[[nodiscard]] auto AdvanceProgress(CustomElementContext &context,
                                   const F32 initial) -> UIResult<void> {
  auto progress = context.State<F32>("progress", initial);
  if (!progress) {
    return progress.Error();
  }
  constexpr auto Step = 0.12F;
  *progress.Value() =
      *progress.Value() + Step > 1.0F ? Step : *progress.Value() + Step;
  return {};
}

[[nodiscard]] auto MoveSelection(CustomElementContext &context,
                                 const NGIN::UIntSize itemCount,
                                 const bool forward) -> UIResult<void> {
  auto selection = context.State<NGIN::UIntSize>("selected-bar", NoSelection);
  if (!selection) {
    return selection.Error();
  }
  if (itemCount == 0) {
    *selection.Value() = NoSelection;
    return {};
  }
  if (*selection.Value() >= itemCount) {
    *selection.Value() = forward ? 0 : itemCount - 1;
  } else if (forward) {
    *selection.Value() = (*selection.Value() + 1) % itemCount;
  } else {
    *selection.Value() =
        *selection.Value() == 0 ? itemCount - 1 : *selection.Value() - 1;
  }
  return {};
}
} // namespace

BadgeElement::BadgeElement(String label, const Color background,
                           const Color foreground)
    : m_label(std::move(label)), m_background(background),
      m_foreground(foreground) {}

auto BadgeElement::Measure(CustomElementContext &,
                           const SizeConstraints constraints)
    -> UIResult<Size> {
  return constraints.Constrain(Size{124.0F, 36.0F});
}

auto BadgeElement::Paint(CustomElementContext &, PaintContext &paint)
    -> UIResult<void> {
  paint.FillRounded(paint.Bounds(), CornerRadius::Uniform(Dp{21.0F}),
                    m_background);
  paint.StrokeRounded(paint.Bounds(), CornerRadius::Uniform(Dp{18.0F}), 1.0F,
                      m_foreground);
  paint.FillRounded(Rect{13.0F, 13.0F, 10.0F, 10.0F},
                    CornerRadius::Uniform(Dp{5.0F}), m_foreground);
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

auto ProgressRingElement::Measure(CustomElementContext &context,
                                  const SizeConstraints constraints)
    -> UIResult<Size> {
  auto progress = context.State<F32>("progress", m_progress);
  if (!progress) {
    return progress.Error();
  }
  return constraints.Constrain(Size{88.0F, 88.0F});
}

auto ProgressRingElement::Paint(CustomElementContext &context,
                                PaintContext &paint) -> UIResult<void> {
  constexpr NGIN::UIntSize SegmentCount = 20;
  const auto extent = paint.Extent();
  const auto center = Point{extent.width * 0.5F, extent.height * 0.5F};
  const auto radius =
      std::max(0.0F, std::min(extent.width, extent.height) * 0.5F - 10.0F);
  const auto *progressState = context.FindState<F32>("progress");
  const auto progress = progressState != nullptr ? *progressState : m_progress;
  const auto active =
      static_cast<NGIN::UIntSize>(std::round(progress * SegmentCount));
  constexpr auto Tau = 6.28318530717958647692F;
  for (NGIN::UIntSize index = 0; index < SegmentCount; ++index) {
    const auto angle =
        -Tau * 0.25F + Tau * static_cast<F32>(index) / SegmentCount;
    const auto x = center.x + std::cos(angle) * radius - 3.0F;
    const auto y = center.y + std::sin(angle) * radius - 3.0F;
    paint.FillRounded(Rect{x, y, 6.0F, 6.0F}, CornerRadius::Uniform(Dp{3.0F}),
                      index < active ? m_value : m_track);
  }
  paint.FillRounded(Rect{center.x - 4.0F, center.y - 4.0F, 8.0F, 8.0F},
                    CornerRadius::Uniform(Dp{4.0F}), m_value);
  if (context.Interaction().focused) {
    paint.StrokeRounded(paint.Bounds(), CornerRadius::Uniform(Dp{10.0F}), 2.0F,
                        m_value);
  }
  return {};
}

auto ProgressRingElement::Semantics(CustomElementContext &context)
    -> UIResult<SemanticProperties> {
  const auto *progressState = context.FindState<F32>("progress");
  const auto progress = progressState != nullptr ? *progressState : m_progress;
  return SemanticProperties{
      .role = SemanticRole::ProgressBar,
      .label = String{"Interactive progress ring"},
      .value = Percentage(progress),
      .range = SemanticRange{.minimum = 0.0,
                             .maximum = 1.0,
                             .current = progress,
                             .step = 0.12},
      .actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus |
                 SemanticActionFlags::Increment,
  };
}

auto ProgressRingElement::PointerEvent(CustomElementContext &context,
                                       RoutedPointerEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target ||
      event.eventKind != RoutedPointerEventKind::ButtonPressed ||
      event.button != PointerButton::Primary) {
    return InvalidationKind::None;
  }
  auto advanced = AdvanceProgress(context, m_progress);
  if (!advanced) {
    return advanced.Error();
  }
  event.Handle();
  return CustomControlInvalidation;
}

auto ProgressRingElement::KeyEvent(CustomElementContext &context,
                                   RoutedKeyEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target || event.state != KeyState::Pressed ||
      (event.logicalKey != LogicalKey::Space &&
       event.logicalKey != LogicalKey::Enter)) {
    return InvalidationKind::None;
  }
  auto advanced = AdvanceProgress(context, m_progress);
  if (!advanced) {
    return advanced.Error();
  }
  event.Handle();
  return CustomControlInvalidation;
}

auto ProgressRingElement::SemanticAction(CustomElementContext &context,
                                         const SemanticActionRequest &request)
    -> UIResult<InvalidationKind> {
  if (request.action != SemanticActionKind::Activate &&
      request.action != SemanticActionKind::Increment) {
    return InvalidationKind::None;
  }
  auto advanced = AdvanceProgress(context, m_progress);
  if (!advanced) {
    return advanced.Error();
  }
  return CustomControlInvalidation;
}

BarChartElement::BarChartElement(std::vector<F32> values, const Color track,
                                 const Color value, const Color selected)
    : m_values(std::move(values)), m_track(track), m_value(value),
      m_selected(selected) {
  for (auto &entry : m_values) {
    entry = std::clamp(entry, 0.0F, 1.0F);
  }
}

auto BarChartElement::Measure(CustomElementContext &context,
                              const SizeConstraints constraints)
    -> UIResult<Size> {
  auto selection = context.State<NGIN::UIntSize>("selected-bar", NoSelection);
  if (!selection) {
    return selection.Error();
  }
  return constraints.Constrain(Size{244.0F, 112.0F});
}

auto BarChartElement::Paint(CustomElementContext &context, PaintContext &paint)
    -> UIResult<void> {
  const auto bounds = paint.Bounds();
  paint.FillRounded(bounds, CornerRadius::Uniform(Dp{10.0F}), m_track);
  if (m_values.empty()) {
    return {};
  }

  const auto *selection = context.FindState<NGIN::UIntSize>("selected-bar");
  const auto selected = selection != nullptr ? *selection : NoSelection;
  constexpr auto padding = 14.0F;
  constexpr auto gap = 8.0F;
  const auto availableWidth =
      std::max(0.0F, bounds.width - padding * 2.0F -
                         gap * static_cast<F32>(m_values.size() - 1));
  const auto barWidth = availableWidth / static_cast<F32>(m_values.size());
  const auto chartHeight =
      std::max(0.0F, bounds.height - padding * 2.0F - 4.0F);
  paint.FillRounded(Rect{padding, bounds.height - padding,
                         bounds.width - padding * 2.0F, 2.0F},
                    CornerRadius::Uniform(Dp{1.0F}), m_value);
  for (NGIN::UIntSize index = 0; index < m_values.size(); ++index) {
    const auto height = chartHeight * m_values[index];
    const auto x = padding + static_cast<F32>(index) * (barWidth + gap);
    const auto y = bounds.height - padding - height;
    paint.FillRounded(Rect{x, y, barWidth, height},
                      CornerRadius::Uniform(Dp{4.0F}),
                      index == selected ? m_selected : m_value);
    if (index == selected) {
      paint.FillRounded(Rect{x + barWidth * 0.5F - 3.0F, y - 9.0F, 6.0F, 6.0F},
                        CornerRadius::Uniform(Dp{3.0F}), m_selected);
    }
  }
  if (context.Interaction().focused) {
    paint.StrokeRounded(bounds, CornerRadius::Uniform(Dp{10.0F}), 2.0F,
                        m_selected);
  }
  return {};
}

auto BarChartElement::Semantics(CustomElementContext &context)
    -> UIResult<SemanticProperties> {
  String value{"No bar selected"};
  const auto *selection = context.FindState<NGIN::UIntSize>("selected-bar");
  if (selection != nullptr && *selection < m_values.size()) {
    const auto number = std::to_string(*selection + 1);
    value = String{"Bar "};
    value.Append(number.c_str());
    value.Append(": ");
    value.Append(Percentage(m_values[*selection]));
  }
  const auto selectedIndex =
      selection != nullptr && *selection < m_values.size() ? *selection : 0;
  return SemanticProperties{
      .role = SemanticRole::Slider,
      .label = String{"Interactive custom bar chart"},
      .value = std::move(value),
      .description = String{"Select a bar or use the arrow keys"},
      .range =
          SemanticRange{
              .minimum = 0.0,
              .maximum = m_values.empty()
                             ? 0.0
                             : static_cast<F64>(m_values.size() - 1),
              .current = static_cast<F64>(selectedIndex),
              .step = 1.0,
          },
      .actions = SemanticActionFlags::Focus | SemanticActionFlags::Increment |
                 SemanticActionFlags::Decrement,
  };
}

auto BarChartElement::PointerEvent(CustomElementContext &context,
                                   RoutedPointerEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target ||
      event.eventKind != RoutedPointerEventKind::ButtonPressed ||
      event.button != PointerButton::Primary || m_values.empty()) {
    return InvalidationKind::None;
  }

  const auto local = context.ToLocal(event.position);
  constexpr auto padding = 14.0F;
  const auto width =
      std::max(1.0F, context.ArrangedSize().width - padding * 2.0F);
  const auto normalized =
      std::clamp((local.x - padding) / width, 0.0F, 0.9999F);
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
  return CustomControlInvalidation;
}

auto BarChartElement::KeyEvent(CustomElementContext &context,
                               RoutedKeyEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target || event.state != KeyState::Pressed ||
      (event.logicalKey != LogicalKey::Left &&
       event.logicalKey != LogicalKey::Right &&
       event.logicalKey != LogicalKey::Up &&
       event.logicalKey != LogicalKey::Down)) {
    return InvalidationKind::None;
  }
  const auto forward = event.logicalKey == LogicalKey::Right ||
                       event.logicalKey == LogicalKey::Down;
  auto moved = MoveSelection(context, m_values.size(), forward);
  if (!moved) {
    return moved.Error();
  }
  event.Handle();
  return CustomControlInvalidation;
}

auto BarChartElement::SemanticAction(CustomElementContext &context,
                                     const SemanticActionRequest &request)
    -> UIResult<InvalidationKind> {
  if (request.action != SemanticActionKind::Increment &&
      request.action != SemanticActionKind::Decrement) {
    return InvalidationKind::None;
  }
  auto moved = MoveSelection(context, m_values.size(),
                             request.action == SemanticActionKind::Increment);
  if (!moved) {
    return moved.Error();
  }
  return CustomControlInvalidation;
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
    paint.FillRounded(Rect{center.x + std::cos(tickAngle) * radius - 2.0F,
                           center.y + std::sin(tickAngle) * radius - 2.0F, 4.0F,
                           4.0F},
                      CornerRadius::Uniform(Dp{2.0F}), m_value);
  }
  const auto delta = Point{endpoint.x - center.x, endpoint.y - center.y};
  const auto steps = std::max(1, static_cast<int>(std::ceil(radius)));
  for (int index = 0; index <= steps; ++index) {
    const auto progress = static_cast<F32>(index) / static_cast<F32>(steps);
    paint.FillRounded(Rect{center.x + delta.x * progress - 2.5F,
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
  NodeProperties demos{};
  demos.layout.preferredSize.width = 712.0F;
  demos.layout.maximumSize.width = 712.0F;
  demos.wrapPanel.itemGap = 12.0F;
  demos.wrapPanel.lineGap = 12.0F;
  demos.wrapPanel.lineAlignment = WrapLineAlignment::Start;
  composer.WrapPanel(
      [&] {
        ComposeDemoSurface(
            composer, text, theme, "Status badge",
            "Custom paint and semantics in one compact status.", 204.0F,
            "badge-demo", [&] {
              NodeProperties badgeOverlay{};
              badgeOverlay.layout.preferredSize = Size{124.0F, 36.0F};
              badgeOverlay.layout.horizontalAlignment =
                  HorizontalAlignment::Start;
              badgeOverlay.layout.verticalAlignment = VerticalAlignment::Center;
              composer.Element(
                  ElementType::Overlay, badgeOverlay,
                  [&] {
                    composer.Custom(std::make_shared<BadgeElement>(
                                        String{"Ready status"},
                                        theme.colors.raisedSurface,
                                        Color{0.24F, 0.82F, 0.58F, 1.0F}),
                                    ExampleProperties(), "badge-paint");
                    ComposeCenteredLabel(
                        composer, text, theme, "READY", Size{124.0F, 36.0F},
                        theme.colors.foreground, "badge-label");
                  },
                  "badge");
            });

        ComposeDemoSurface(
            composer, text, theme, "Progress control",
            "Click it or press Space to advance the value.", 204.0F,
            "progress-demo", [&] {
              auto progressProperties = ExampleProperties();
              progressProperties.layout.horizontalAlignment =
                  HorizontalAlignment::Center;
              progressProperties.interaction.focusable = true;
              composer.Custom(
                  std::make_shared<ProgressRingElement>(
                      0.68F, theme.colors.border, theme.colors.focus),
                  progressProperties, "progress-ring");
            });

        ComposeDemoSurface(
            composer, text, theme, "Interactive chart",
            "Select a bar or use the arrow keys to explore values.", 276.0F,
            "chart-demo", [&] {
              auto chartProperties = ExampleProperties();
              chartProperties.interaction.focusable = true;
              composer.Custom(std::make_shared<BarChartElement>(
                                  std::vector<F32>{0.28F, 0.72F, 0.46F, 0.92F,
                                                   0.61F, 0.84F},
                                  theme.colors.raisedSurface,
                                  theme.colors.accent, theme.colors.focus),
                              chartProperties, "bar-chart");
            });
      },
      demos, "custom-examples");
}
} // namespace NGIN::UIGallery
