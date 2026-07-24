#include <NGIN/UI/Controls.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto ControlSize(const SizeConstraints constraints,
                               const Size preferred) noexcept -> Size {
  return constraints.Constrain(preferred);
}

[[nodiscard]] auto ErrorResult(const ControlPresentation &presentation,
                               UIError error) -> UIResult<void> {
  if (presentation.onError) {
    presentation.onError(error);
  }
  return std::move(error);
}

[[nodiscard]] auto ValueText(const F32 value) -> NGIN::Text::String {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << value;
  return NGIN::Text::String{output.str().c_str()};
}

[[nodiscard]] auto LocalContains(const CustomElementContext &context,
                                 const Point point) noexcept -> bool {
  const auto local = context.ToLocal(point);
  const auto size = context.ArrangedSize();
  return local.x >= 0.0F && local.y >= 0.0F && local.x <= size.width &&
         local.y <= size.height;
}

struct PressState final {
  bool pressed{false};
};

[[nodiscard]] auto IsPressed(CustomElementContext &context) noexcept -> bool {
  const auto *state = context.FindState<PressState>("press");
  return state != nullptr && state->pressed;
}

template <typename Activate>
auto RoutePress(CustomElementContext &context, RoutedPointerEvent &event,
                Activate &&activate) -> UIResult<InvalidationKind> {
  auto state = context.State<PressState>("press");
  if (!state) {
    return std::move(state).Error();
  }
  if (event.button != PointerButton::Primary &&
      (event.eventKind == RoutedPointerEventKind::ButtonPressed ||
       event.eventKind == RoutedPointerEventKind::ButtonReleased)) {
    return InvalidationKind::None;
  }
  if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
      context.Interaction().enabled) {
    state.Value()->pressed = true;
    event.CapturePointer();
    event.Handle();
    return InvalidationKind::Paint;
  }
  if (event.eventKind == RoutedPointerEventKind::ButtonReleased &&
      state.Value()->pressed) {
    state.Value()->pressed = false;
    event.ReleasePointerCapture();
    event.Handle();
    if (context.Interaction().enabled &&
        LocalContains(context, event.position)) {
      auto result = std::forward<Activate>(activate)();
      if (!result) {
        return std::move(result).Error();
      }
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }
  return InvalidationKind::None;
}

class CheckBoxElement final : public ICustomElement {
public:
  CheckBoxElement(Binding<CheckState> value, ControlPresentation presentation,
                  SemanticProperties semantics)
      : m_value(std::move(value)), m_presentation(std::move(presentation)),
        m_semantics(std::move(semantics)) {}

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return ControlSize(constraints, Size{24.0F, 24.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &colors = m_presentation.theme.colors;
    const auto interaction = context.Interaction();
    auto background = colors.sunkenSurface;
    auto border = colors.border;
    if (!interaction.enabled) {
      background = colors.disabledSurface;
      border = colors.disabledForeground;
    } else if (interaction.hovered) {
      border = colors.focus;
    }
    const auto state = m_value.Get();
    if (state != CheckState::Unchecked) {
      background = interaction.enabled ? colors.accent : colors.disabledSurface;
    }
    if (interaction.enabled && IsPressed(context)) {
      background = state == CheckState::Unchecked ? colors.raisedSurface
                                                  : colors.accentPressed;
    }
    if (m_presentation.invalid) {
      border = colors.error;
    }
    const auto bounds = paint.Bounds();
    paint.FillRounded(bounds, CornerRadius::Uniform(Dp{5.0F}), background);
    paint.StrokeRounded(bounds, CornerRadius::Uniform(Dp{5.0F}), 1.5F, border);
    if (state == CheckState::Checked) {
      const auto markColor = interaction.enabled ? colors.accentForeground
                                                 : colors.disabledForeground;
      const auto unit = std::max(2.0F, bounds.width / 8.0F);
      paint.Fill(Rect{unit * 1.5F, unit * 3.5F, unit, unit}, markColor);
      paint.Fill(Rect{unit * 2.5F, unit * 4.5F, unit, unit}, markColor);
      paint.Fill(Rect{unit * 3.5F, unit * 3.5F, unit, unit}, markColor);
      paint.Fill(Rect{unit * 4.5F, unit * 2.5F, unit, unit}, markColor);
      paint.Fill(Rect{unit * 5.5F, unit * 1.5F, unit, unit}, markColor);
    } else if (state == CheckState::Indeterminate) {
      const auto mark = Rect{bounds.width * 0.22F, bounds.height * 0.42F,
                             bounds.width * 0.56F, bounds.height * 0.16F};
      paint.FillRounded(mark, CornerRadius::Uniform(Dp{2.0F}),
                        interaction.enabled ? colors.accentForeground
                                            : colors.disabledForeground);
    }
    if (interaction.focused) {
      paint.StrokeRounded(bounds, CornerRadius::Uniform(Dp{5.0F}), 2.0F,
                          colors.focus);
    }
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    auto result = m_semantics;
    result.role = SemanticRole::CheckBox;
    result.actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus;
    if (m_value.Get() == CheckState::Checked) {
      result.states |= SemanticStateFlags::Checked;
      result.value = NGIN::Text::String{"checked"};
    } else if (m_value.Get() == CheckState::Indeterminate) {
      result.states |= SemanticStateFlags::Indeterminate;
      result.value = NGIN::Text::String{"mixed"};
    } else {
      result.value = NGIN::Text::String{"unchecked"};
    }
    return result;
  }

  auto PointerEvent(CustomElementContext &context, RoutedPointerEvent &event)
      -> UIResult<InvalidationKind> override {
    return RoutePress(context, event, [this] { return Toggle(); });
  }

  auto KeyEvent(CustomElementContext &context, RoutedKeyEvent &event)
      -> UIResult<InvalidationKind> override {
    if (context.Interaction().enabled && event.state == KeyState::Pressed &&
        (event.logicalKey == LogicalKey::Space ||
         event.logicalKey == LogicalKey::Enter)) {
      event.Handle();
      auto toggled = Toggle();
      if (!toggled) {
        return std::move(toggled).Error();
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

private:
  auto Toggle() -> UIResult<void> {
    const auto next = m_value.Get() == CheckState::Checked
                          ? CheckState::Unchecked
                          : CheckState::Checked;
    auto changed = m_value.Set(next);
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).Error());
    }
    return {};
  }

  Binding<CheckState> m_value;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

class RadioButtonElement final : public ICustomElement {
public:
  RadioButtonElement(RadioSelection selection, ControlPresentation presentation,
                     SemanticProperties semantics)
      : m_selection(std::move(selection)),
        m_presentation(std::move(presentation)),
        m_semantics(std::move(semantics)) {}

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return ControlSize(constraints, Size{24.0F, 24.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &colors = m_presentation.theme.colors;
    const auto interaction = context.Interaction();
    const auto bounds = paint.Bounds();
    const auto radius = CornerRadius::Uniform(Dp{bounds.width * 0.5F});
    const auto border =
        m_presentation.invalid
            ? colors.error
            : (IsPressed(context)
                   ? colors.accentPressed
                   : (interaction.hovered ? colors.focus : colors.border));
    paint.FillRounded(bounds, radius,
                      interaction.enabled ? colors.sunkenSurface
                                          : colors.disabledSurface);
    paint.StrokeRounded(bounds, radius, 1.5F,
                        interaction.enabled ? border
                                            : colors.disabledForeground);
    if (m_selection.isSelected && m_selection.isSelected()) {
      const auto inset = bounds.width * 0.27F;
      paint.FillRounded(Rect{inset, inset, bounds.width - inset * 2.0F,
                             bounds.height - inset * 2.0F},
                        CornerRadius::Uniform(Dp{bounds.width * 0.5F}),
                        interaction.enabled ? colors.accent
                                            : colors.disabledForeground);
    }
    if (interaction.focused) {
      paint.StrokeRounded(bounds, radius, 2.0F, colors.focus);
    }
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    auto result = m_semantics;
    result.role = SemanticRole::RadioButton;
    result.actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus;
    if (m_selection.isSelected && m_selection.isSelected()) {
      result.states |=
          SemanticStateFlags::Selected | SemanticStateFlags::Checked;
      result.value = NGIN::Text::String{"selected"};
    } else {
      result.value = NGIN::Text::String{"not selected"};
    }
    return result;
  }

  auto PointerEvent(CustomElementContext &context, RoutedPointerEvent &event)
      -> UIResult<InvalidationKind> override {
    return RoutePress(context, event, [this] { return Select(); });
  }

  auto KeyEvent(CustomElementContext &context, RoutedKeyEvent &event)
      -> UIResult<InvalidationKind> override {
    if (context.Interaction().enabled && event.state == KeyState::Pressed &&
        (event.logicalKey == LogicalKey::Space ||
         event.logicalKey == LogicalKey::Enter)) {
      event.Handle();
      auto selected = Select();
      if (!selected) {
        return std::move(selected).Error();
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

private:
  auto Select() -> UIResult<void> {
    if (!m_selection.select) {
      return ErrorResult(m_presentation,
                         MakeUIError(UIErrorCode::InvalidState,
                                     "Radio selection is not writable",
                                     "NGIN.UI", "RadioButton"));
    }
    auto changed = m_selection.select();
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).Error());
    }
    return {};
  }

  RadioSelection m_selection;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

class ToggleSwitchElement final : public ICustomElement {
public:
  ToggleSwitchElement(Binding<bool> value, ControlPresentation presentation,
                      SemanticProperties semantics)
      : m_value(std::move(value)), m_presentation(std::move(presentation)),
        m_semantics(std::move(semantics)) {}

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return ControlSize(constraints, Size{46.0F, 26.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto interaction = context.Interaction();
    const auto &colors = m_presentation.theme.colors;
    const auto bounds = paint.Bounds();
    const auto on = m_value.Get();
    auto track = on ? colors.accent : colors.sunkenSurface;
    if (!interaction.enabled) {
      track = colors.disabledSurface;
    } else if (IsPressed(context)) {
      track = on ? colors.accentPressed : colors.raisedSurface;
    } else if (interaction.hovered && on) {
      track = colors.accentHovered;
    }
    const auto border = m_presentation.invalid ? colors.error : colors.border;
    paint.FillRounded(bounds, CornerRadius::Uniform(Dp{bounds.height * 0.5F}),
                      track);
    paint.StrokeRounded(
        bounds, CornerRadius::Uniform(Dp{bounds.height * 0.5F}), 1.0F,
        interaction.enabled ? border : colors.disabledForeground);
    const auto diameter = std::max(8.0F, bounds.height - 8.0F);
    const auto x = on ? bounds.width - diameter - 4.0F : 4.0F;
    paint.FillRounded(Rect{x, 4.0F, diameter, diameter},
                      CornerRadius::Uniform(Dp{diameter * 0.5F}),
                      interaction.enabled ? colors.accentForeground
                                          : colors.disabledForeground);
    if (interaction.focused) {
      paint.StrokeRounded(bounds,
                          CornerRadius::Uniform(Dp{bounds.height * 0.5F}), 2.0F,
                          colors.focus);
    }
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    auto result = m_semantics;
    result.role = SemanticRole::Switch;
    result.actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus;
    result.value = NGIN::Text::String{m_value.Get() ? "on" : "off"};
    if (m_value.Get()) {
      result.states |= SemanticStateFlags::Checked;
    }
    return result;
  }

  auto PointerEvent(CustomElementContext &context, RoutedPointerEvent &event)
      -> UIResult<InvalidationKind> override {
    return RoutePress(context, event, [this] { return Toggle(); });
  }

  auto KeyEvent(CustomElementContext &context, RoutedKeyEvent &event)
      -> UIResult<InvalidationKind> override {
    if (context.Interaction().enabled && event.state == KeyState::Pressed &&
        (event.logicalKey == LogicalKey::Space ||
         event.logicalKey == LogicalKey::Enter)) {
      event.Handle();
      auto toggled = Toggle();
      if (!toggled) {
        return std::move(toggled).Error();
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

private:
  auto Toggle() -> UIResult<void> {
    auto changed = m_value.Set(!m_value.Get());
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).Error());
    }
    return {};
  }

  Binding<bool> m_value;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

struct SliderDragState final {
  bool dragging{false};
};

class SliderElement final : public ICustomElement {
public:
  SliderElement(Binding<F32> value, SliderRange range,
                ControlPresentation presentation, SemanticProperties semantics)
      : m_value(std::move(value)), m_range(range),
        m_presentation(std::move(presentation)),
        m_semantics(std::move(semantics)) {
    if (m_range.maximum < m_range.minimum) {
      std::swap(m_range.minimum, m_range.maximum);
    }
    m_range.step = std::max(0.0001F, std::abs(m_range.step));
  }

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return ControlSize(constraints, Size{220.0F, 28.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto interaction = context.Interaction();
    const auto &colors = m_presentation.theme.colors;
    const auto bounds = paint.Bounds();
    const auto centerY = bounds.height * 0.5F;
    const auto thumbRadius = std::min(9.0F, bounds.height * 0.4F);
    const auto start = thumbRadius;
    const auto length = std::max(0.0F, bounds.width - thumbRadius * 2.0F);
    const auto fraction = Fraction();
    paint.FillRounded(Rect{start, centerY - 2.0F, length, 4.0F},
                      CornerRadius::Uniform(Dp{2.0F}), colors.sunkenSurface);
    paint.FillRounded(Rect{start, centerY - 2.0F, length * fraction, 4.0F},
                      CornerRadius::Uniform(Dp{2.0F}),
                      interaction.enabled ? colors.accent
                                          : colors.disabledForeground);
    const auto thumb =
        Rect{start + length * fraction - thumbRadius, centerY - thumbRadius,
             thumbRadius * 2.0F, thumbRadius * 2.0F};
    const auto *drag = context.FindState<SliderDragState>("drag");
    const auto dragging = drag != nullptr && drag->dragging;
    paint.FillRounded(thumb, CornerRadius::Uniform(Dp{thumbRadius}),
                      interaction.enabled
                          ? (dragging
                                 ? colors.accentPressed
                                 : (interaction.hovered ? colors.accentHovered
                                                        : colors.accent))
                          : colors.disabledSurface);
    paint.StrokeRounded(thumb, CornerRadius::Uniform(Dp{thumbRadius}), 1.0F,
                        m_presentation.invalid ? colors.error : colors.border);
    if (interaction.focused) {
      paint.StrokeRounded(thumb, CornerRadius::Uniform(Dp{thumbRadius}), 2.0F,
                          colors.focus);
    }
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    auto result = m_semantics;
    result.role = SemanticRole::Slider;
    result.value = ValueText(m_value.Get());
    result.range = SemanticRange{
        .minimum = static_cast<F64>(m_range.minimum),
        .maximum = static_cast<F64>(m_range.maximum),
        .current = static_cast<F64>(m_value.Get()),
        .step = static_cast<F64>(m_range.step),
    };
    result.actions =
        SemanticActionFlags::Focus | SemanticActionFlags::SetValue |
        SemanticActionFlags::Increment | SemanticActionFlags::Decrement;
    return result;
  }

  auto PointerEvent(CustomElementContext &context, RoutedPointerEvent &event)
      -> UIResult<InvalidationKind> override {
    auto drag = context.State<SliderDragState>("drag");
    if (!drag) {
      return std::move(drag).Error();
    }
    if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
        event.button == PointerButton::Primary &&
        context.Interaction().enabled) {
      drag.Value()->dragging = true;
      event.CapturePointer();
      event.Handle();
      auto changed = SetFromPoint(context, event.position);
      if (!changed) {
        return std::move(changed).Error();
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    if (event.eventKind == RoutedPointerEventKind::Moved &&
        drag.Value()->dragging) {
      event.Handle();
      auto changed = SetFromPoint(context, event.position);
      if (!changed) {
        return std::move(changed).Error();
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    if (event.eventKind == RoutedPointerEventKind::ButtonReleased &&
        event.button == PointerButton::Primary && drag.Value()->dragging) {
      drag.Value()->dragging = false;
      event.ReleasePointerCapture();
      event.Handle();
      auto changed = SetFromPoint(context, event.position);
      if (!changed) {
        return std::move(changed).Error();
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

  auto KeyEvent(CustomElementContext &context, RoutedKeyEvent &event)
      -> UIResult<InvalidationKind> override {
    if (!context.Interaction().enabled || event.state == KeyState::Released) {
      return InvalidationKind::None;
    }
    auto next = m_value.Get();
    if (event.logicalKey == LogicalKey::Left ||
        event.logicalKey == LogicalKey::Down) {
      next -= m_range.step;
    } else if (event.logicalKey == LogicalKey::Right ||
               event.logicalKey == LogicalKey::Up) {
      next += m_range.step;
    } else if (event.logicalKey == LogicalKey::Home) {
      next = m_range.minimum;
    } else if (event.logicalKey == LogicalKey::End) {
      next = m_range.maximum;
    } else {
      return InvalidationKind::None;
    }
    event.Handle();
    auto changed = SetValue(next);
    if (!changed) {
      return std::move(changed).Error();
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

private:
  [[nodiscard]] auto Fraction() const noexcept -> F32 {
    const auto extent = m_range.maximum - m_range.minimum;
    return extent > 0.0F
               ? std::clamp((m_value.Get() - m_range.minimum) / extent, 0.0F,
                            1.0F)
               : 0.0F;
  }

  auto SetFromPoint(const CustomElementContext &context, const Point point)
      -> UIResult<void> {
    const auto local = context.ToLocal(point);
    const auto width = std::max(1.0F, context.ArrangedSize().width);
    return SetValue(m_range.minimum + std::clamp(local.x / width, 0.0F, 1.0F) *
                                          (m_range.maximum - m_range.minimum));
  }

  auto SetValue(F32 value) -> UIResult<void> {
    value = std::clamp(value, m_range.minimum, m_range.maximum);
    value = m_range.minimum +
            std::round((value - m_range.minimum) / m_range.step) * m_range.step;
    value = std::clamp(value, m_range.minimum, m_range.maximum);
    auto changed = m_value.Set(value);
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).Error());
    }
    return {};
  }

  Binding<F32> m_value;
  SliderRange m_range;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

class ProgressBarElement final : public ICustomElement {
public:
  ProgressBarElement(ProgressValue value, ControlPresentation presentation,
                     SemanticProperties semantics)
      : m_value(value), m_presentation(std::move(presentation)),
        m_semantics(std::move(semantics)) {}

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return ControlSize(constraints, Size{220.0F, 12.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto interaction = context.Interaction();
    const auto &colors = m_presentation.theme.colors;
    const auto bounds = paint.Bounds();
    const auto radius = CornerRadius::Uniform(Dp{bounds.height * 0.5F});
    paint.FillRounded(bounds, radius,
                      interaction.enabled ? colors.sunkenSurface
                                          : colors.disabledSurface);
    const auto foreground =
        interaction.enabled ? colors.accent : colors.disabledForeground;
    if (m_value.indeterminate) {
      const auto chunkWidth = bounds.width * 0.28F;
      paint.FillRounded(
          Rect{bounds.width * 0.36F, 0.0F, chunkWidth, bounds.height}, radius,
          foreground);
    } else {
      paint.FillRounded(
          Rect{0.0F, 0.0F, bounds.width * Fraction(), bounds.height}, radius,
          foreground);
    }
    if (m_presentation.invalid) {
      paint.StrokeRounded(bounds, radius, 1.0F, colors.error);
    }
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    auto result = m_semantics;
    result.role = SemanticRole::ProgressBar;
    result.value = m_value.indeterminate ? NGIN::Text::String{"indeterminate"}
                                         : ValueText(m_value.value);
    if (m_value.indeterminate) {
      result.states |= SemanticStateFlags::Indeterminate;
    } else {
      result.range = SemanticRange{
          .minimum = static_cast<F64>(m_value.minimum),
          .maximum = static_cast<F64>(m_value.maximum),
          .current = static_cast<F64>(m_value.value),
      };
    }
    return result;
  }

private:
  [[nodiscard]] auto Fraction() const noexcept -> F32 {
    const auto extent = m_value.maximum - m_value.minimum;
    return extent > 0.0F
               ? std::clamp((m_value.value - m_value.minimum) / extent, 0.0F,
                            1.0F)
               : 0.0F;
  }

  ProgressValue m_value;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

[[nodiscard]] auto PrepareProperties(NodeProperties properties,
                                     const Size preferred, const bool focusable)
    -> NodeProperties {
  if (properties.layout.preferredSize.width <= 0.0F) {
    properties.layout.preferredSize.width = preferred.width;
  }
  if (properties.layout.preferredSize.height <= 0.0F) {
    properties.layout.preferredSize.height = preferred.height;
  }
  properties.interaction.focusable = focusable;
  return properties;
}
} // namespace

void CheckBox(Composer &composer, Binding<CheckState> value,
              const ControlPresentation &presentation,
              const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{24.0F, 24.0F}, true);
  composer.Custom(std::make_shared<CheckBoxElement>(
                      std::move(value), presentation, control.semantics),
                  control, key);
}

void RadioButton(Composer &composer, RadioSelection selection,
                 const ControlPresentation &presentation,
                 const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{24.0F, 24.0F}, true);
  composer.Custom(std::make_shared<RadioButtonElement>(
                      std::move(selection), presentation, control.semantics),
                  control, key);
}

void ToggleSwitch(Composer &composer, Binding<bool> value,
                  const ControlPresentation &presentation,
                  const NodeProperties &properties,
                  const std::string_view key) {
  auto control = PrepareProperties(properties, Size{46.0F, 26.0F}, true);
  composer.Custom(std::make_shared<ToggleSwitchElement>(
                      std::move(value), presentation, control.semantics),
                  control, key);
}

void Slider(Composer &composer, Binding<F32> value, const SliderRange range,
            const ControlPresentation &presentation,
            const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{220.0F, 28.0F}, true);
  composer.Custom(std::make_shared<SliderElement>(
                      std::move(value), range, presentation, control.semantics),
                  control, key);
}

void ProgressBar(Composer &composer, const ProgressValue value,
                 const ControlPresentation &presentation,
                 const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{220.0F, 12.0F}, false);
  composer.Custom(std::make_shared<ProgressBarElement>(value, presentation,
                                                       control.semantics),
                  control, key);
}

void Label(Composer &composer, NGIN::Text::String value, ITextLayout &layout,
           IGlyphAtlas &glyphAtlas, const std::string_view identifier,
           const std::string_view targetIdentifier,
           const NodeProperties &properties, const std::string_view key) {
  auto label = properties;
  label.semantics.role = SemanticRole::Text;
  label.semantics.identifier = NGIN::Text::String{identifier};
  label.semantics.labelFor = NGIN::Text::String{targetIdentifier};
  label.semantics.label = value;
  label.interaction.hitTestVisible = false;
  composer.Text(std::move(value), layout, glyphAtlas, label, key);
}

struct ToolTipController::State final {
  Window *window{nullptr};
  NGIN::Text::String content{};
  std::chrono::milliseconds delay{500};
  Window::ScheduledActionId pending{0};
  Rect anchor{};
  bool open{false};
};

ToolTipController::ToolTipController(Window &window, NGIN::Text::String content,
                                     const std::chrono::milliseconds delay)
    : m_state(std::make_shared<State>(State{
          .window = &window,
          .content = std::move(content),
          .delay = std::max(delay, std::chrono::milliseconds{0}),
      })) {}

void ToolTipController::Attach(NodeProperties &target) {
  if (target.semantics.description.Empty()) {
    target.semantics.description = m_state->content;
  }
  auto previous = std::move(target.interaction.onPointer);
  std::weak_ptr<State> weak = m_state;
  target.interaction.onPointer =
      [weak = std::move(weak),
       previous = std::move(previous)](RoutedPointerEvent &event) mutable {
        if (previous) {
          previous(event);
        }
        const auto state = weak.lock();
        if (!state || state->window == nullptr || state->window->IsClosed()) {
          return;
        }
        if (event.eventKind == RoutedPointerEventKind::Entered ||
            event.eventKind == RoutedPointerEventKind::Moved) {
          state->anchor = Rect{event.position.x, event.position.y, 1.0F, 1.0F};
        }
        if (event.eventKind == RoutedPointerEventKind::Entered) {
          if (state->pending != 0) {
            static_cast<void>(state->window->CancelScheduled(state->pending));
          }
          auto scheduled = state->window->Schedule(state->delay, [weak] {
            const auto scheduledState = weak.lock();
            if (!scheduledState || scheduledState->window == nullptr ||
                scheduledState->window->IsClosed()) {
              return;
            }
            scheduledState->pending = 0;
            scheduledState->open = true;
            scheduledState->window->Invalidate(InvalidationKind::Compose |
                                               InvalidationKind::Paint |
                                               InvalidationKind::Semantics);
          });
          state->pending = scheduled ? scheduled.Value() : 0;
        } else if (event.eventKind == RoutedPointerEventKind::Exited) {
          if (state->pending != 0) {
            static_cast<void>(state->window->CancelScheduled(state->pending));
            state->pending = 0;
          }
          if (state->open) {
            state->open = false;
            state->window->Invalidate(InvalidationKind::Compose |
                                      InvalidationKind::Paint |
                                      InvalidationKind::Semantics);
          }
        }
      };
}

auto ToolTipController::IsOpen() const noexcept -> bool {
  return m_state->open;
}

void ToolTipController::Dismiss() noexcept {
  if (m_state->window == nullptr || m_state->window->IsClosed()) {
    return;
  }
  if (m_state->pending != 0) {
    static_cast<void>(m_state->window->CancelScheduled(m_state->pending));
    m_state->pending = 0;
  }
  if (m_state->open) {
    m_state->open = false;
    m_state->window->Invalidate(InvalidationKind::Compose |
                                InvalidationKind::Paint |
                                InvalidationKind::Semantics);
  }
}

auto ToolTipController::Anchor() const noexcept -> Rect {
  return m_state->anchor;
}
} // namespace NGIN::UI
