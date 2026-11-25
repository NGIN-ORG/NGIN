#include "GallerySamplePicker.hpp"
#include <NGIN/UIGallery/CustomControls.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>

namespace NGIN::UIGallery::Pages {
using namespace UI;
using Text::String;
using namespace std::chrono_literals;

namespace {
enum class Study { Timing, Choreography, Loop, Feedback };
enum class Curve { Standard, Linear, EaseOut, Keyframes, Bezier };

// The graph uses its own high-contrast plotting palette in both app themes.
constexpr Color PlotInk{0.075F, 0.11F, 0.20F, 1};
constexpr Color PlotGrid{0.20F, 0.255F, 0.345F, 1};
constexpr Color PlotFill{0.15F, 0.24F, 0.42F, 1};
constexpr Color PlotLine{1.0F, 0.62F, 0.38F, 1};
constexpr Color PlotMarker{0.52F, 0.74F, 1.0F, 1};

auto Tween(std::chrono::milliseconds duration, EasingCurve curve)
    -> AnimationSpec {
  return {.timing =
              TweenTiming{.duration = duration, .curve = std::move(curve)}};
}

// Consecutive keyframes with the same position create a visible pause while
// the clock keeps moving. The motion engine accepts this as a custom curve.
class HoldCurve final : public IEasingCurve {
public:
  auto Evaluate(F32 time) const -> F32 override {
    constexpr std::array<Point, 4> frames{
        {{0, 0}, {0.24F, 0.55F}, {0.68F, 0.55F}, {1, 1}}};
    for (std::size_t i = 1; i < frames.size(); ++i) {
      const auto from = frames[i - 1];
      const auto to = frames[i];
      if (time <= to.x)
        return std::lerp(from.y, to.y, (time - from.x) / (to.x - from.x));
    }
    return frames.back().y;
  }
};

void Disc(PaintContext &paint, Point center, F32 radius, Color color) {
  paint.FillRounded(
      {center.x - radius, center.y - radius, 2 * radius, 2 * radius},
      CornerRadius::Uniform(Dp{radius}), color);
}

// The controller animates a unit interval. Painting maps it into the actual
// arranged width, so a running study keeps its position when the window
// resizes.
class MotionPlot final : public ICustomElement {
public:
  MotionPlot(String description, Color marker,
             std::optional<EasingCurve> curve = {})
      : m_description(std::move(description)), m_marker(marker),
        m_curve(std::move(curve)) {
    // Sample once per curve change. A frame only evaluates its moving cursor.
    if (m_curve) {
      for (std::size_t i = 0; i < m_samples.size(); ++i) {
        const auto time = static_cast<F32>(i) / (m_samples.size() - 1);
        m_samples[i] = m_curve->Evaluate(time);
        m_minimum = std::min(m_minimum, m_samples[i]);
        m_maximum = std::max(m_maximum, m_samples[i]);
      }
    }
  }

  auto Measure(CustomElementContext &, SizeConstraints space)
      -> UIResult<Size> override {
    return space.Constrain({620, m_curve ? 182.0F : 56.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    const auto bounds = paint.Bounds();
    paint.FillRounded(bounds, CornerRadius::Uniform(Dp{8}), PlotInk);
    const auto pad = std::min(22.0F, bounds.width * 0.12F);
    const auto span = bounds.width - 2 * pad;
    if (span < 1 || bounds.height < 8)
      return {};

    const auto progress = std::clamp(context.MotionValue(), 0.0F, 1.0F);
    const auto railY = m_curve ? bounds.height - 21 : bounds.height / 2;
    const auto position = m_curve ? m_curve->Evaluate(progress) : progress;
    paint.Fill({pad, railY - 1, span, 2}, PlotGrid);
    for (int tick = 0; tick <= 10; ++tick) {
      paint.Fill({pad + span * static_cast<F32>(tick) / 10, railY - 4, 1, 8},
                 PlotGrid);
    }
    if (m_curve) {
      PaintCurve(paint, pad, span, railY - 22, progress);
    }
    Disc(paint, {pad + Fraction(position) * span, railY}, 8, m_marker);
    return {};
  }

  auto Semantics(CustomElementContext &context)
      -> UIResult<SemanticProperties> override {
    const auto time = std::clamp(context.MotionValue(), 0.0F, 1.0F);
    const auto position = m_curve ? m_curve->Evaluate(time) : time;
    return SemanticProperties{
        .role = SemanticRole::Image,
        .label = m_description,
        .value = String{std::to_string(std::lround(position * 100)) + "%"}};
  }

private:
  // Fit custom curves that travel before their start or beyond their endpoint.
  auto Fraction(F32 position) const -> F32 {
    return std::clamp((position - m_minimum) / (m_maximum - m_minimum), 0.0F,
                      1.0F);
  }

  void PaintCurve(PaintContext &paint, F32 left, F32 width, F32 bottom,
                  F32 time) const {
    const auto height = bottom - 24;
    if (height <= 0)
      return;
    for (int line = 0; line <= 4; ++line) {
      const auto fraction = static_cast<F32>(line) / 4;
      paint.Fill({left + width * fraction, 16, 1, bottom - 16}, PlotGrid);
      paint.Fill({left, bottom - height * fraction, width, 1}, PlotGrid);
    }
    const int samples = std::clamp(static_cast<int>(width / 3), 12, 160);
    const auto step = width / static_cast<F32>(samples);
    const auto zero = bottom - height * Fraction(0);
    for (int i = 0; i < samples; ++i) {
      const auto fraction =
          (static_cast<F32>(i) + 0.5F) / static_cast<F32>(samples);
      const auto index = std::lround(fraction * (m_samples.size() - 1));
      const auto y = bottom - height * Fraction(m_samples[index]);
      const auto x = left + static_cast<F32>(i) * step;
      paint.Fill({x, std::min(zero, y), step + 0.5F, std::abs(zero - y)},
                 PlotFill);
      paint.Fill({x, y - 1.5F, step + 0.5F, 3}, PlotLine);
    }
    const Point cursor{left + time * width,
                       bottom - height * Fraction(m_curve->Evaluate(time))};
    paint.Fill(
        {cursor.x, std::min(cursor.y, bottom), 1, std::abs(bottom - cursor.y)},
        PlotMarker);
    Disc(paint, cursor, 6, m_marker);
    Disc(paint, cursor, 2.5F, PlotInk);
  }

  String m_description;
  Color m_marker;
  std::optional<EasingCurve> m_curve;
  std::array<F32, 161> m_samples{};
  F32 m_minimum = 0;
  F32 m_maximum = 1;
};

auto Stack(std::vector<Views::View> children, F32 gap = 10) -> Views::View {
  NodeProperties layout{};
  layout.layout.gap = gap;
  return Views::Column(std::move(children), layout);
}

auto Plot(String label, Color marker, MotionProperties motion,
          std::string_view key, F32 height,
          std::optional<EasingCurve> curve = {}) -> Views::View {
  NodeProperties box{};
  box.layout.maximumSize.height = height;
  box.motion = std::move(motion);
  return Views::Custom(std::make_shared<MotionPlot>(std::move(label), marker,
                                                    std::move(curve)),
                       box)
      .Key(key);
}

auto CurveSlider(const Theme &theme, const char *name, Binding<F32> value,
                 SliderRange range) -> Views::View {
  NodeProperties control{};
  control.semantics.label = String{name};
  control.layout.preferredSize.width = 140;
  std::ostringstream caption;
  caption << name << "  " << std::fixed << std::setprecision(2) << value.Get();
  return Stack({TextView(String{caption.str()}, 13,
                         theme.colors.mutedForeground, "name"),
                Views::Slider(std::move(value), range, {.theme = theme},
                              control)},
               3)
      .Key(name);
}

auto TimingStudy(GalleryViewModel &model, const Theme &theme) -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    const auto preset = scope.Local("curve", Curve::Standard);
    const auto curve = [&] {
      switch (preset.Get()) {
      case Curve::Linear:
        return EasingCurve::Linear();
      case Curve::EaseOut:
        return EasingCurve::EaseOut();
      case Curve::Keyframes:
        return EasingCurve::MakeCustom<HoldCurve>();
      case Curve::Bezier:
        return EasingCurve::CubicBezier(model.BezierX1(), model.BezierY1(),
                                        model.BezierX2(), model.BezierY2());
      default:
        return EasingCurve::Standard();
      }
    }();
    // Animate time linearly. The same easing function drives the graph and
    // the traveller, so the plot doubles as a faithful preview of the curve.
    MotionProperties motion{};
    motion.value = Animate(model.MotionForward() ? 1.0F : 0.0F,
                           Tween(900ms, EasingCurve::Linear()));
    auto stage =
        Plot("Position over time", PlotMarker, std::move(motion),
             "motion-stage", model.IsCompact() ? 124.0F : 182.0F, curve);
    auto play = ButtonView(
        theme, model.MotionForward() ? "Play in reverse" : "Play curve",
        [&model] { model.ToggleMotionTarget(); }, "play-curve", 148);
    auto duration = TextProperties(14, theme.colors.mutedForeground);
    duration.layout.verticalAlignment = VerticalAlignment::Center;
    std::vector<Views::View> content{
        TextView("Position over time", 21, theme.colors.foreground, "title",
                 SemanticRole::Heading),
        std::move(stage),
        ActionRow(
            {std::move(play), Views::Label("900 ms", duration).Key("duration")},
            "playback"),
        SamplePicker<Curve>(
            theme, preset, "Timing curve",
            {{Curve::Standard, "Standard", "curve-standard"},
             {Curve::Linear, "Linear", "curve-linear"},
             {Curve::EaseOut, "Ease out", "curve-out"},
             {Curve::Keyframes, "Keyframe hold", "curve-keyframes"},
             {Curve::Bezier, "Custom Bézier", "curve-custom"}})};
    if (preset.Get() == Curve::Bezier) {
      const SliderRange horizontal{.minimum = 0, .maximum = 1, .step = 0.05F};
      const SliderRange vertical{
          .minimum = -0.5F, .maximum = 1.5F, .step = 0.05F};
      content.push_back(ActionRow(
          {CurveSlider(theme, "Start X", model.BezierX1Binding(), horizontal),
           CurveSlider(theme, "Start Y", model.BezierY1Binding(), vertical),
           CurveSlider(theme, "End X", model.BezierX2Binding(), horizontal),
           CurveSlider(theme, "End Y", model.BezierY2Binding(), vertical)},
          "curve-handles"));
    }
    return Stack(std::move(content));
  });
}

auto ChoreographyStudy(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  MotionProperties first{}, second{};
  model.AsyncPrimaryMotion().Attach(first);
  model.AsyncSecondaryMotion().Attach(second);
  const auto trackHeight = model.IsCompact() ? 44.0F : 56.0F;
  auto status = Views::Component([&model, theme] {
    return TextView(model.AsyncMotionStatus(), 18, theme.colors.foreground,
                    "async-status");
  });
  return Stack(
      {std::move(status),
       Plot("First track", PlotMarker, std::move(first), "async-primary",
            trackHeight),
       Plot("Second track", PlotLine, std::move(second), "async-secondary",
            trackHeight),
       ActionRow(
           {ButtonView(
                theme, "Run steps",
                [&model] { model.StartAsyncMotionSequence(); }, "sequence",
                112),
            ButtonView(
                theme, "Move together",
                [&model] { model.StartParallelMotion(); }, "parallel", 150, ButtonAppearance::Neutral),
            ButtonView(
                theme, "Slow move", [&model] { model.StartCancelableMotion(); },
                "slow", 122, ButtonAppearance::Neutral),
            ButtonView(
                theme, "Retarget orange",
                [&model] { model.InterruptAsyncMotion(); }, "interrupt", 158, ButtonAppearance::Neutral),
            ButtonView(
                theme, "Stop", [&model] { model.CancelAsyncMotion(); },
                "cancel", 80, ButtonAppearance::Neutral)},
           "playback")});
}

auto LoopStudy(GalleryViewModel &model, const Theme &theme) -> Views::View {
  MotionProperties motion{};
  const auto running = model.MotionRepeatRunning();
  if (running && model.MotionRepeatHandle()) {
    auto timing = Tween(580ms, EasingCurve::EaseInOut());
    timing.repeatCount = 3;
    timing.repeatMode = AnimationRepeatMode::Reverse;
    motion.Set(MotionDialSweep,
               AnimateFrom(0.0F, 1.0F, timing, *model.MotionRepeatHandle()));
    motion.onSettled = [&model] { model.FinishMotionRepeat(); };
  }
  NodeProperties dial{};
  dial.layout.minimumSize.height = 144;
  dial.motion = std::move(motion);
  return Stack(
      {TextView("Reversing loop", 21, theme.colors.foreground, "title",
                SemanticRole::Heading),
       Views::Custom(std::make_shared<MotionDialElement>(PlotInk, PlotLine),
                     dial)
           .Key("repeat-marker"),
       ActionRow(
           {ButtonView(
                theme, "Start loop", [&model] { model.StartMotionRepeat(); },
                "start-repeat", 112, ButtonAppearance::Accent, !running),
            ButtonView(
                theme, "Stop", [&model] { model.CancelMotionRepeat(); },
                "stop-repeat", 80, ButtonAppearance::Neutral, running)},
           "playback")});
}

auto FeedbackStudy(GalleryViewModel &model, const Theme &theme) -> Views::View {
  const auto timing = Tween(520ms, EasingCurve::EaseOut());
  NodeProperties button{};
  button.layout.preferredSize = {156, theme.controls.regularHeight};
  button.layout.padding = {14, 8, 14, 8};
  button.visual = MakeButtonVisual(theme);
  button.semantics.label = "Open animated popup";
  NodeProperties popup{};
  popup.semantics.label = "Animated popup";
  const auto open = model.MotionPopup().IsOpen();
  popup.motion.opacity =
      open ? AnimateFrom(0.0F, 1.0F, timing) : Animate(0.0F, timing);
  popup.motion.translation = open ? AnimateFrom(Point{0, 36}, Point{}, timing)
                                  : Animate(Point{0, 36}, timing);
  NodeProperties menu{};
  menu.layout.gap = 6;
  menu.layout.preferredSize.width = 210;
  menu.layout.padding = Thickness::Uniform(Dp{12});
  menu.visual = MakePanelVisual(theme);
  auto popupButton =
      Views::MenuButton(
          model.MotionPopup(), "motion-popup-anchor",
          TextView("Open popup", 14, theme.colors.accentForeground, "label"),
          Views::Column(
              {TextView("Rise and fade", 18, theme.colors.foreground,
                        "popup-title", SemanticRole::Heading),
               ButtonView(
                   theme, "Close", [&model] { model.MotionPopup().Close(); },
                   "close", 90)},
              menu)
              .Key("popup-card"),
          button, popup)
          .Key("motion-popup");
  NodeProperties progress{};
  progress.semantics.label = "Loading";
  progress.layout.maximumSize.width = 410;
  return Stack(
      {TextView("Interface feedback", 21, theme.colors.foreground, "title",
                SemanticRole::Heading),
       TextView("Loading", 14, theme.colors.mutedForeground, "progress-label"),
       Views::ProgressBar({.indeterminate = true}, {.theme = theme}, progress)
           .Key("motion-progress"),
       std::move(popupButton)});
}
} // namespace

auto MotionView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    const auto selected = scope.Local("study", Study::Timing);
    // Switch studies through a binding so pointer, keyboard and semantic
    // activation all retire the old controller before its track is unmounted.
    const auto selection =
        Binding<Study>{[selected]() -> const Study & { return selected.Get(); },
                       [selected, &model](Study next) {
                         if (selected.Get() == next)
                           return UIResult<void>{};
                         if (selected.Get() == Study::Loop)
                           model.CancelMotionRepeat();
                         if (selected.Get() == Study::Choreography)
                           model.CancelAsyncMotion();
                         if (selected.Get() == Study::Feedback)
                           model.MotionPopup().Close();
                         return selected.Set(next);
                       }};
    auto picker = SamplePicker<Study>(
        theme, selection, "Motion study",
        {{Study::Timing, "Timing curves", "study-timing"},
         {Study::Choreography, "Choreography", "study-choreography"},
         {Study::Loop, "Loops", "study-loop"},
         {Study::Feedback, "UI feedback", "study-feedback"}});
    const auto experiment = [&] {
      switch (selected.Get()) {
      case Study::Choreography:
        return ChoreographyStudy(model, theme);
      case Study::Loop:
        return LoopStudy(model, theme);
      case Study::Feedback:
        return FeedbackStudy(model, theme);
      default:
        return TimingStudy(model, theme);
      }
    }();
    auto heading = model.IsCompact()
                       ? TextView("Motion", 30, theme.colors.foreground,
                                  "page-title", SemanticRole::Heading)
                       : PageHeadingView(theme, "Motion",
                                         "Compare timing curves, coordinate "
                                         "tracks and preview reduced motion.");
    std::vector<Views::View> content{
        std::move(heading), std::move(picker),
        experiment.Key("experiment-" +
                       std::to_string(static_cast<int>(selected.Get()))),
        ButtonView(
            theme,
            model.MotionPreviewReduced() ? "Restore motion" : "Reduce motion",
            [&model] { model.ToggleMotionPreviewReduced(); }, "reduced-motion",
            160, ButtonAppearance::Subtle)};
    if (model.IsCompact())
      return Stack(std::move(content), 12).Key("gallery-page-content");
    return PageContentView(std::move(content));
  });
}
} // namespace NGIN::UIGallery::Pages
