#include "ControlPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace NGIN::UI {
namespace {
// The common control entry points supply a natural size for unspecified axes.
class SizedControlElement : public ICustomElement {
public:
  explicit SizedControlElement(Size preferred) : m_preferred(preferred) {}
  auto Measure(CustomElementContext &, SizeConstraints constraints)
      -> UIResult<Size> final {
    return constraints.Constrain(m_preferred);
  }

private:
  Size m_preferred;
};

[[nodiscard]] auto ErrorResult(const ControlPresentation &presentation,
                               UIError error) -> UIResult<void> {
  if (presentation.onError) {
    presentation.onError(error);
  }
  return std::unexpected(std::move(error));
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

// Keep the visible indicator centered inside its larger pointer target.
[[nodiscard]] auto IndicatorBounds(Size available, Size preferred) -> Rect {
  if (preferred.width <= 0 || preferred.height <= 0)
    return {};
  const F32 scale = std::min({1.0F, available.width / preferred.width,
                              available.height / preferred.height});
  const Size size{preferred.width * scale, preferred.height * scale};
  return {(available.width - size.width) / 2,
          (available.height - size.height) / 2, size.width, size.height};
}

[[nodiscard]] auto Pill(Rect bounds) -> CornerRadius {
  return CornerRadius::Uniform(Dp{std::min(bounds.width, bounds.height) / 2});
}

[[nodiscard]] auto TrackColor(const Theme &theme, CustomInteractionState input,
                              bool pressed, bool active) -> Color {
  const auto &c = theme.colors;
  if (!input.enabled)
    return c.disabledSurface;
  if (active)
    return pressed ? c.accentPressed
                   : (input.hovered ? c.accentHovered : c.accent);
  return pressed ? c.sunkenSurface : c.raisedSurface;
}

[[nodiscard]] auto IndicatorBorder(const ControlPresentation &presentation,
                                   CustomInteractionState input, bool pressed,
                                   bool active) -> Color {
  const auto &c = presentation.theme.colors;
  if (!input.enabled)
    return c.disabledForeground;
  if (presentation.invalid)
    return c.error;
  if (pressed || active)
    return c.accentPressed;
  return input.hovered ? c.focus : c.mutedForeground;
}

void FillControl(PaintContext &paint, Rect bounds, CornerRadius corners,
                 const Theme &theme, Color color, bool accent) {
  if (accent && theme.gradientControls) {
    const auto brush = MakeAccentBrush(theme, color);
    paint.FillGradient(bounds, corners, std::get<LinearGradient>(brush));
  } else {
    paint.FillRounded(bounds, corners, color);
  }
}

void PaintFocus(PaintContext &paint, Rect face, F32 radius, const Theme &theme,
                CustomInteractionState input) {
  if (!input.focused || !input.enabled)
    return;
  const F32 gap = theme.controls.focusOffset;
  paint.StrokeRounded(
      {face.x - gap, face.y - gap, face.width + 2 * gap, face.height + 2 * gap},
      CornerRadius::Uniform(Dp{radius + gap}), theme.controls.focusThickness,
      theme.colors.focus);
}

template <typename Activate>
auto RoutePress(CustomElementContext &context, RoutedPointerEvent &event,
                Activate &&activate) -> UIResult<InvalidationKind> {
  auto state = context.State<PressState>("press");
  if (!state) {
    return std::unexpected(std::move(state).error());
  }
  if (event.button != PointerButton::Primary &&
      (event.eventKind == RoutedPointerEventKind::ButtonPressed ||
       event.eventKind == RoutedPointerEventKind::ButtonReleased)) {
    return InvalidationKind::None;
  }
  if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
      context.Interaction().enabled) {
    state.value()->pressed = true;
    event.CapturePointer();
    event.Handle();
    return InvalidationKind::Paint;
  }
  if (event.eventKind == RoutedPointerEventKind::ButtonReleased &&
      state.value()->pressed) {
    state.value()->pressed = false;
    event.ReleasePointerCapture();
    event.Handle();
    if (context.Interaction().enabled &&
        LocalContains(context, event.position)) {
      auto result = std::forward<Activate>(activate)();
      if (!result) {
        return std::unexpected(std::move(result).error());
      }
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }
  return InvalidationKind::None;
}

class CheckBoxElement final : public SizedControlElement {
public:
  CheckBoxElement(Binding<CheckState> value, ControlPresentation presentation,
                  const NodeProperties &properties)
      : SizedControlElement(properties.layout.preferredSize),
        m_value(std::move(value)), m_presentation(std::move(presentation)),
        m_semantics(properties.semantics) {}

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &theme = m_presentation.theme;
    const auto input = context.Interaction();
    const auto state = m_value.Get();
    const bool active = state != CheckState::Unchecked;
    const bool pressed = IsPressed(context);
    const auto face = IndicatorBounds(paint.Extent(), {20, 20});
    const auto radius = std::min(theme.radii.small, face.width / 2);
    const auto corners = CornerRadius::Uniform(Dp{radius});
    FillControl(paint, face, corners, theme,
                TrackColor(theme, input, pressed, active),
                active && input.enabled);
    paint.StrokeRounded(
        face, corners, theme.controls.borderThickness,
        IndicatorBorder(m_presentation, input, pressed, active));
    const auto ink = input.enabled ? theme.colors.accentForeground
                                   : theme.colors.disabledForeground;
    const auto point = [&](F32 x, F32 y) -> Point {
      return {face.x + x * face.width, face.y + y * face.height};
    };
    if (state == CheckState::Checked) {
      const auto joint = point(.42F, .68F);
      paint.Line(point(.23F, .49F), joint, face.width * .12F, ink);
      paint.Line(joint, point(.78F, .32F), face.width * .12F, ink);
    } else if (state == CheckState::Indeterminate) {
      paint.Line(point(.26F, .50F), point(.74F, .50F), face.width * .13F, ink);
    }
    PaintFocus(paint, face, radius, theme, input);
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
        return std::unexpected(std::move(toggled).error());
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

  auto SemanticAction(CustomElementContext &context,
                      const SemanticActionRequest &request)
      -> UIResult<InvalidationKind> override {
    if (request.action != SemanticActionKind::Activate ||
        !context.Interaction().enabled) {
      return std::unexpected(
          MakeUIError(UIErrorCode::Unsupported,
                      "The checkbox cannot perform this semantic action",
                      "NGIN.UI", "CheckBox::SemanticAction"));
    }
    auto toggled = Toggle();
    if (!toggled) {
      return std::unexpected(std::move(toggled).error());
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

private:
  auto Toggle() -> UIResult<void> {
    const auto next = m_value.Get() == CheckState::Checked
                          ? CheckState::Unchecked
                          : CheckState::Checked;
    auto changed = m_value.Set(next);
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).error());
    }
    return {};
  }

  Binding<CheckState> m_value;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

class RadioButtonElement final : public SizedControlElement {
public:
  RadioButtonElement(RadioSelection selection, ControlPresentation presentation,
                     const NodeProperties &properties)
      : SizedControlElement(properties.layout.preferredSize),
        m_selection(std::move(selection)),
        m_presentation(std::move(presentation)),
        m_semantics(properties.semantics) {}

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &theme = m_presentation.theme;
    const auto input = context.Interaction();
    const bool active = m_selection.isSelected && m_selection.isSelected();
    const bool pressed = IsPressed(context);
    const auto face = IndicatorBounds(paint.Extent(), {20, 20});
    FillControl(paint, face, Pill(face), theme,
                TrackColor(theme, input, pressed, active),
                active && input.enabled);
    paint.StrokeRounded(
        face, Pill(face), theme.controls.borderThickness,
        IndicatorBorder(m_presentation, input, pressed, active));
    if (active) {
      const F32 dot = face.width * 0.44F;
      const auto mark = IndicatorBounds(paint.Extent(), {dot, dot});
      paint.FillRounded(mark, Pill(mark),
                        input.enabled ? theme.colors.accentForeground
                                      : theme.colors.disabledForeground);
    }
    PaintFocus(paint, face, face.width / 2, theme, input);
    return {};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    auto result = m_semantics;
    result.role = SemanticRole::RadioButton;
    result.actions = SemanticActionFlags::Activate |
                     SemanticActionFlags::Focus | SemanticActionFlags::Select;
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
        return std::unexpected(std::move(selected).error());
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

  auto SemanticAction(CustomElementContext &context,
                      const SemanticActionRequest &request)
      -> UIResult<InvalidationKind> override {
    if ((request.action != SemanticActionKind::Activate &&
         request.action != SemanticActionKind::Select) ||
        !context.Interaction().enabled) {
      return std::unexpected(
          MakeUIError(UIErrorCode::Unsupported,
                      "The radio button cannot perform this semantic action",
                      "NGIN.UI", "RadioButton::SemanticAction"));
    }
    auto selected = Select();
    if (!selected) {
      return std::unexpected(std::move(selected).error());
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
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
      return ErrorResult(m_presentation, std::move(changed).error());
    }
    return {};
  }

  RadioSelection m_selection;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

class ToggleSwitchElement final : public SizedControlElement {
public:
  ToggleSwitchElement(Binding<bool> value, ControlPresentation presentation,
                      const NodeProperties &properties)
      : SizedControlElement(properties.layout.preferredSize),
        m_value(std::move(value)), m_presentation(std::move(presentation)),
        m_semantics(properties.semantics) {}

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &theme = m_presentation.theme;
    const auto input = context.Interaction();
    const bool on = m_value.Get();
    const bool pressed = IsPressed(context);
    const auto track = IndicatorBounds(paint.Extent(), {42, 22});
    FillControl(paint, track, Pill(track), theme,
                TrackColor(theme, input, pressed, on), on && input.enabled);
    paint.StrokeRounded(track, Pill(track), theme.controls.borderThickness,
                        IndicatorBorder(m_presentation, input, pressed, on));
    const F32 inset = track.height * 0.14F;
    const F32 diameter = track.height - 2 * inset;
    // Widen toward the center while pressed and keep the outer edge anchored.
    const F32 width = diameter + (pressed ? inset : 0);
    const Rect thumb{on ? track.x + track.width - inset - width
                        : track.x + inset,
                     track.y + inset, width, diameter};
    const auto ink =
        on ? theme.colors.accentForeground : theme.colors.mutedForeground;
    paint.FillRounded(thumb, Pill(thumb),
                      input.enabled ? ink : theme.colors.disabledForeground);
    PaintFocus(paint, track, track.height / 2, theme, input);
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
        return std::unexpected(std::move(toggled).error());
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

  auto SemanticAction(CustomElementContext &context,
                      const SemanticActionRequest &request)
      -> UIResult<InvalidationKind> override {
    if (request.action != SemanticActionKind::Activate ||
        !context.Interaction().enabled) {
      return std::unexpected(
          MakeUIError(UIErrorCode::Unsupported,
                      "The switch cannot perform this semantic action",
                      "NGIN.UI", "ToggleSwitch::SemanticAction"));
    }
    auto toggled = Toggle();
    if (!toggled) {
      return std::unexpected(std::move(toggled).error());
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

private:
  auto Toggle() -> UIResult<void> {
    auto changed = m_value.Set(!m_value.Get());
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).error());
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

[[nodiscard]] auto SliderInset(Size size) -> F32 {
  return std::min({12.0F, size.width / 2, size.height / 2});
}

class SliderElement final : public SizedControlElement {
public:
  SliderElement(Binding<F32> value, SliderRange range,
                ControlPresentation presentation,
                const NodeProperties &properties)
      : SizedControlElement(properties.layout.preferredSize),
        m_value(std::move(value)), m_range(range),
        m_presentation(std::move(presentation)),
        m_semantics(properties.semantics) {
    if (m_range.maximum < m_range.minimum) {
      std::swap(m_range.minimum, m_range.maximum);
    }
    m_range.step = std::max(0.0001F, std::abs(m_range.step));
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &theme = m_presentation.theme;
    const auto &c = theme.colors;
    const auto input = context.Interaction();
    const auto size = paint.Extent();
    const auto inset = SliderInset(size);
    const auto length = size.width - 2 * inset;
    const auto centerY = size.height / 2;
    const auto thickness = std::min(5.0F, size.height);
    const Rect track{inset, centerY - thickness / 2, length, thickness};
    paint.FillRounded(track, Pill(track),
                      input.enabled ? c.border : c.disabledSurface);
    const auto fraction = Fraction();
    const Rect level{track.x, track.y, length * fraction, thickness};
    FillControl(paint, level, Pill(level), theme,
                input.enabled ? c.accent : c.disabledForeground, input.enabled);
    const auto *drag = context.FindState<SliderDragState>("drag");
    const bool pressed = drag && drag->dragging;
    const auto radius = inset * (input.hovered || pressed ? 0.78F : 0.70F);
    const auto centerX = inset + length * fraction;
    const Rect thumb{centerX - radius, centerY - radius, radius * 2,
                     radius * 2};
    const auto face = TrackColor(theme, input, pressed, true);
    FillControl(paint, thumb, Pill(thumb), theme, face, input.enabled);
    paint.StrokeRounded(thumb, Pill(thumb), theme.controls.borderThickness,
                        IndicatorBorder(m_presentation, input, pressed, true));
    const auto coreRadius = radius * .45F;
    const Rect core{centerX - coreRadius, centerY - coreRadius, coreRadius * 2,
                    coreRadius * 2};
    paint.FillRounded(core, Pill(core),
                      input.enabled ? c.accentForeground
                                    : c.disabledForeground);
    PaintFocus(paint, thumb, radius, theme, input);
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
      return std::unexpected(std::move(drag).error());
    }
    if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
        event.button == PointerButton::Primary &&
        context.Interaction().enabled) {
      drag.value()->dragging = true;
      event.CapturePointer();
      event.Handle();
      auto changed = SetFromPoint(context, event.position);
      if (!changed) {
        return std::unexpected(std::move(changed).error());
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    if (event.eventKind == RoutedPointerEventKind::Moved &&
        drag.value()->dragging) {
      event.Handle();
      auto changed = SetFromPoint(context, event.position);
      if (!changed) {
        return std::unexpected(std::move(changed).error());
      }
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    if (event.eventKind == RoutedPointerEventKind::ButtonReleased &&
        event.button == PointerButton::Primary && drag.value()->dragging) {
      drag.value()->dragging = false;
      event.ReleasePointerCapture();
      event.Handle();
      auto changed = SetFromPoint(context, event.position);
      if (!changed) {
        return std::unexpected(std::move(changed).error());
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
      return std::unexpected(std::move(changed).error());
    }
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

  auto SemanticAction(CustomElementContext &context,
                      const SemanticActionRequest &request)
      -> UIResult<InvalidationKind> override {
    if (!context.Interaction().enabled) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidState,
                                         "The slider is disabled", "NGIN.UI",
                                         "Slider::SemanticAction"));
    }
    auto next = m_value.Get();
    switch (request.action) {
    case SemanticActionKind::SetValue:
      next = static_cast<F32>(request.numericValue);
      break;
    case SemanticActionKind::Increment:
      next += m_range.step;
      break;
    case SemanticActionKind::Decrement:
      next -= m_range.step;
      break;
    default:
      return std::unexpected(
          MakeUIError(UIErrorCode::Unsupported,
                      "The slider cannot perform this semantic action",
                      "NGIN.UI", "Slider::SemanticAction"));
    }
    auto changed = SetValue(next);
    if (!changed) {
      return std::unexpected(std::move(changed).error());
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
    const auto size = context.ArrangedSize();
    const auto inset = SliderInset(size);
    const auto length = size.width - 2 * inset;
    if (length <= 0)
      return {};
    const auto fraction = std::clamp((local.x - inset) / length, 0.0F, 1.0F);
    return SetValue(m_range.minimum +
                    fraction * (m_range.maximum - m_range.minimum));
  }

  auto SetValue(F32 value) -> UIResult<void> {
    value = std::clamp(value, m_range.minimum, m_range.maximum);
    value = m_range.minimum +
            std::round((value - m_range.minimum) / m_range.step) * m_range.step;
    value = std::clamp(value, m_range.minimum, m_range.maximum);
    auto changed = m_value.Set(value);
    if (!changed) {
      return ErrorResult(m_presentation, std::move(changed).error());
    }
    return {};
  }

  Binding<F32> m_value;
  SliderRange m_range;
  ControlPresentation m_presentation;
  SemanticProperties m_semantics;
};

class ProgressBarElement final : public SizedControlElement {
public:
  ProgressBarElement(ProgressValue value, ControlPresentation presentation,
                     const NodeProperties &properties)
      : SizedControlElement(properties.layout.preferredSize), m_value(value),
        m_presentation(std::move(presentation)),
        m_semantics(properties.semantics) {}

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto &theme = m_presentation.theme;
    const auto &colors = theme.colors;
    const bool enabled = context.Interaction().enabled;
    const auto track = paint.Bounds();
    paint.FillRounded(track, Pill(track),
                      enabled ? colors.border : colors.disabledSurface);
    F32 start = 0;
    F32 end = track.width * std::clamp(context.MotionValue(), 0.0F, 1.0F);
    if (m_value.indeterminate) {
      const auto chunk = track.width * .28F;
      const auto phase = context.IsMotionActive()
                             ? std::clamp(context.MotionValue(), 0.0F, 1.0F)
                             : .5F;
      start = -chunk + (track.width + chunk) * phase;
      end = std::min(track.width, start + chunk);
      start = std::max(0.0F, start);
    }
    if (end > start) {
      const Rect value{start, 0, end - start, track.height};
      FillControl(paint, value, Pill(value), theme,
                  enabled ? colors.accent : colors.disabledForeground, enabled);
    }
    if (m_presentation.invalid)
      paint.StrokeRounded(track, Pill(track), theme.controls.borderThickness,
                          colors.error);
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
  composer.Custom(std::make_shared<CheckBoxElement>(std::move(value),
                                                    presentation, control),
                  control, key);
}

void RadioButton(Composer &composer, RadioSelection selection,
                 const ControlPresentation &presentation,
                 const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{24.0F, 24.0F}, true);
  composer.Custom(std::make_shared<RadioButtonElement>(std::move(selection),
                                                       presentation, control),
                  control, key);
}

void ToggleSwitch(Composer &composer, Binding<bool> value,
                  const ControlPresentation &presentation,
                  const NodeProperties &properties,
                  const std::string_view key) {
  auto control = PrepareProperties(properties, Size{46.0F, 26.0F}, true);
  composer.Custom(std::make_shared<ToggleSwitchElement>(std::move(value),
                                                        presentation, control),
                  control, key);
}

void Slider(Composer &composer, Binding<F32> value, const SliderRange range,
            const ControlPresentation &presentation,
            const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{220.0F, 28.0F}, true);
  composer.Custom(std::make_shared<SliderElement>(std::move(value), range,
                                                  presentation, control),
                  control, key);
}

void ProgressBar(Composer &composer, const ProgressValue value,
                 const ControlPresentation &presentation,
                 const NodeProperties &properties, const std::string_view key) {
  auto control = PrepareProperties(properties, Size{220.0F, 12.0F}, false);
  if (!control.motion.value) {
    if (value.indeterminate) {
      control.motion.value =
          AnimateFrom(0.0F, 1.0F,
                      AnimationSpec{
                          .timing =
                              TweenTiming{
                                  .duration = std::chrono::milliseconds{1200},
                                  .curve = EasingCurve::Linear(),
                              },
                          .repeatCount = 0,
                          .repeatMode = AnimationRepeatMode::Restart,
                      });
    } else {
      const auto extent = value.maximum - value.minimum;
      const auto fraction =
          extent > 0.0F
              ? std::clamp((value.value - value.minimum) / extent, 0.0F, 1.0F)
              : 0.0F;
      control.motion.value = Animate(
          fraction,
          AnimationSpec{
              .timing =
                  TweenTiming{
                      .duration = std::chrono::milliseconds{static_cast<Int64>(
                          presentation.theme.motion.regularMilliseconds)},
                      .curve = EasingCurve::Standard(),
                  },
          });
    }
  }
  composer.Custom(
      std::make_shared<ProgressBarElement>(value, presentation, control),
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
  State(Window &owner, NGIN::Text::String description,
        std::chrono::milliseconds timeout)
      : window(&owner), content(std::move(description)),
        delay(std::max(timeout, std::chrono::milliseconds{0})) {}
  Window *window{nullptr};
  NGIN::Text::String content{};
  std::chrono::milliseconds delay{500};
  Window::ScheduledActionId pending{0};
  Rect anchor{};
  UI::State<bool> open{false};
};

ToolTipController::ToolTipController(Window &window, NGIN::Text::String content,
                                     const std::chrono::milliseconds delay)
    : m_state(std::make_shared<State>(window, std::move(content), delay)) {}

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
            static_cast<void>(scheduledState->open.Set(true));
            scheduledState->window->Invalidate(InvalidationKind::Compose |
                                               InvalidationKind::Paint |
                                               InvalidationKind::Semantics);
          });
          state->pending = scheduled ? scheduled.value() : 0;
        } else if (event.eventKind == RoutedPointerEventKind::Exited) {
          if (state->pending != 0) {
            static_cast<void>(state->window->CancelScheduled(state->pending));
            state->pending = 0;
          }
          if (state->open.Get()) {
            static_cast<void>(state->open.Set(false));
            state->window->Invalidate(InvalidationKind::Compose |
                                      InvalidationKind::Paint |
                                      InvalidationKind::Semantics);
          }
        }
      };
}

auto ToolTipController::IsOpen() const noexcept -> bool {
  return m_state->open.Get();
}

void ToolTipController::Dismiss() noexcept {
  if (m_state->window == nullptr || m_state->window->IsClosed()) {
    return;
  }
  if (m_state->pending != 0) {
    static_cast<void>(m_state->window->CancelScheduled(m_state->pending));
    m_state->pending = 0;
  }
  if (m_state->open.Get()) {
    static_cast<void>(m_state->open.Set(false));
    m_state->window->Invalidate(InvalidationKind::Compose |
                                InvalidationKind::Paint |
                                InvalidationKind::Semantics);
  }
}

auto ToolTipController::Anchor() const noexcept -> Rect {
  return m_state->anchor;
}
} // namespace NGIN::UI
