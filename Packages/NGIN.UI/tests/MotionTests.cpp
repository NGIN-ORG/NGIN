#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Collections.hpp>
#include <NGIN/UI/Motion.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace std::chrono_literals;

namespace MotionTest {
struct GaugeValue final {
  NGIN::F32 sweep{0.0F};
  auto operator<=>(const GaugeValue &) const noexcept = default;
};
} // namespace MotionTest

namespace NGIN::UI {
template <> struct AnimationInterpolator<MotionTest::GaugeValue> final {
  [[nodiscard]] static auto Interpolate(
      const MotionTest::GaugeValue start, const MotionTest::GaugeValue end,
      const F32 progress) noexcept -> MotionTest::GaugeValue {
    return MotionTest::GaugeValue{
        .sweep = AnimationInterpolator<F32>::Interpolate(
            start.sweep, end.sweep, progress),
    };
  }
};
} // namespace NGIN::UI

namespace {
using namespace NGIN::UI;

inline const AnimationProperty<MotionTest::GaugeValue> GaugeSweep{
    "MotionTests.Gauge.Sweep", MotionTest::GaugeValue{}};
inline const AnimationProperty<NGIN::F32> OpacityCollision{
    "NGIN.UI.Opacity", 0.0F};

class OvershootCurve final : public IEasingCurve {
public:
  [[nodiscard]] auto Evaluate(const NGIN::F32 progress) const
      -> NGIN::F32 override {
    return progress * 1.25F;
  }
  [[nodiscard]] auto Name() const noexcept -> std::string_view override {
    return "TestOvershoot";
  }
};

class ThrowingCurve final : public IEasingCurve {
public:
  [[nodiscard]] auto Evaluate(NGIN::F32) const -> NGIN::F32 override {
    throw std::runtime_error{"expected"};
  }
};

class NonFiniteCurve final : public IEasingCurve {
public:
  [[nodiscard]] auto Evaluate(NGIN::F32) const -> NGIN::F32 override {
    return std::numeric_limits<NGIN::F32>::quiet_NaN();
  }
};

class GaugeProbeElement final : public ICustomElement {
public:
  explicit GaugeProbeElement(NGIN::F32 &observed) : m_observed(&observed) {}

  auto Measure(CustomElementContext &, const SizeConstraints constraints)
      -> UIResult<Size> override {
    return constraints.Constrain(Size{100.0F, 40.0F});
  }

  auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> override {
    *m_observed = context.MotionValue(GaugeSweep).sweep;
    paint.Fill(paint.Bounds(), Color{0.2F, 0.5F, 0.9F, 1.0F});
    return {};
  }

private:
  NGIN::F32 *m_observed{nullptr};
};
} // namespace

TEST_CASE("extensible curves validate inputs and preserve finite overshoot") {
  using namespace NGIN::UI;

  const auto bezier = EasingCurve::CubicBezier(0.42F, 0.0F, 0.58F, 1.0F);
  CHECK(ApplyEasing(bezier, 0.0F) == 0.0F);
  CHECK(ApplyEasing(bezier, 0.5F) == Catch::Approx(0.5F).margin(0.0002F));
  CHECK(ApplyEasing(bezier, 1.0F) == 1.0F);

  const auto stepsEnd = EasingCurve::Steps(4, StepPosition::End);
  const auto stepsStart = EasingCurve::Steps(4, StepPosition::Start);
  CHECK(ApplyEasing(stepsEnd, 0.24F) == 0.0F);
  CHECK(ApplyEasing(stepsEnd, 0.25F) == 0.25F);
  CHECK(ApplyEasing(stepsStart, 0.01F) == 0.25F);

  const auto overshoot = EasingCurve::MakeCustom<OvershootCurve>();
  CHECK(overshoot.IsCustom());
  CHECK(overshoot.Name().compare("TestOvershoot") == 0);
  CHECK(ApplyEasing(overshoot, 1.0F) == 1.25F);
  CHECK(ApplyEasing(overshoot, -1.0F) == 0.0F);
  CHECK(ApplyEasing(overshoot, 2.0F) == 1.25F);

  CHECK(ApplyEasing(EasingCurve::MakeCustom<ThrowingCurve>(), 0.4F) ==
        0.4F);
  CHECK(ApplyEasing(EasingCurve::MakeCustom<NonFiniteCurve>(), 0.6F) ==
        0.6F);
  CHECK(EasingCurve::Custom(nullptr) == EasingCurve::Standard());
}

TEST_CASE("motion interpolation and handles are deterministic") {
  using namespace NGIN::UI;

  CHECK(ApplyEasing(EasingCurve::Linear(), 0.0F) == 0.0F);
  CHECK(ApplyEasing(EasingCurve::Linear(), 0.25F) == 0.25F);
  CHECK(ApplyEasing(EasingCurve::Linear(), 1.0F) == 1.0F);
  CHECK(ApplyEasing(EasingCurve::Standard(), 0.5F) == 0.5F);
  CHECK(Interpolate(10.0F, 30.0F, 0.0F) == 10.0F);
  CHECK(Interpolate(10.0F, 30.0F, 0.25F) == 15.0F);
  CHECK(Interpolate(10.0F, 30.0F, 1.0F) == 30.0F);
  CHECK(Interpolate(Point{0.0F, 10.0F}, Point{20.0F, 30.0F}, 0.5F) ==
        Point{10.0F, 20.0F});
  const auto color = Interpolate(Color{0.0F, 0.2F, 0.4F, 0.6F},
                                 Color{1.0F, 0.6F, 0.8F, 1.0F}, 0.5F);
  CHECK(color.red == 0.5F);
  CHECK(color.green == Catch::Approx(0.4F));
  CHECK(color.alpha == 0.8F);

  AnimationHandle handle;
  auto target = Animate(1.0F, AnimationSpec{}, handle);
  CHECK_FALSE(target.IsCancelled());
  auto moved = std::move(handle);
  moved.Cancel();
  CHECK(target.IsCancelled());
}

TEST_CASE("custom typed properties share scheduling diagnostics and policy") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *clock = platform.get();
  auto *recording = renderer.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::move(renderer),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"CustomMotion"},
      .title = NGIN::Text::String{"Custom motion"},
      .initialSize = PixelSize{160, 80},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  NGIN::F32 target = 100.0F;
  NGIN::F32 observed = -1.0F;
  AnimationHandle handle;
  const auto curve = EasingCurve::MakeCustom<OvershootCurve>();
  const auto spec = AnimationSpec{
      .timing = TweenTiming{.duration = 100ms, .curve = curve},
  };
  window->SetContent([&](Composer &composer) {
    NodeProperties node{};
    node.layout.preferredSize = Size{100.0F, 40.0F};
    node.motion.opacity = AnimateFrom(0.0F, 1.0F, spec);
    node.motion.Set(OpacityCollision, Animate(0.25F, spec));
    node.motion.Set(GaugeSweep,
                    AnimateFrom(MotionTest::GaugeValue{},
                                MotionTest::GaugeValue{.sweep = target}, spec,
                                handle));
    composer.Custom(std::make_shared<GaugeProbeElement>(observed), node,
                    "gauge");
  });

  REQUIRE(application->PumpOnce().HasValue());
  CHECK(observed == 0.0F);
  clock->AdvanceTime(90ms);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(observed == Catch::Approx(112.5F).margin(0.01F));
  REQUIRE_FALSE(recording->RenderPackets().empty());
  CHECK(recording->RenderPackets().back().vertices.front().color >> 24U ==
        255U);

  const auto &diagnostics = window->Diagnostics().motion;
  const auto customTrack = std::find_if(
      diagnostics.tracks.begin(), diagnostics.tracks.end(),
      [](const MotionTrackDiagnostics &track) {
        return track.property == GaugeSweep.Id();
      });
  REQUIRE(customTrack != diagnostics.tracks.end());
  CHECK(customTrack->propertyName.compare("MotionTests.Gauge.Sweep") == 0);
  CHECK_FALSE(customTrack->interpolator.empty());
  CHECK(customTrack->timing.compare("Tween") == 0);
  CHECK(customTrack->curve.compare("TestOvershoot") == 0);
  CHECK(customTrack->customCurve);
  CHECK(customTrack->active);
  CHECK(diagnostics.propertyConflictCount > 0);

  target = 200.0F;
  window->Invalidate(InvalidationKind::Compose);
  REQUIRE(application->PumpOnce().HasValue());
  clock->AdvanceTime(50ms);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(observed == Catch::Approx(167.1875F).margin(0.02F));

  handle.Cancel();
  window->Invalidate(InvalidationKind::Paint);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(window->HasActiveAnimations());
}

TEST_CASE("spring timing is deterministic and settles at its exact target") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *clock = platform.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"SpringMotion"},
      .title = NGIN::Text::String{"Spring motion"},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  NGIN::F32 observed = -1.0F;
  bool visible = true;
  window->SetContent([&](Composer &composer) {
    if (!visible) {
      return;
    }
    NodeProperties node{};
    node.motion.Set(
        GaugeSweep,
        AnimateFrom(MotionTest::GaugeValue{},
                    MotionTest::GaugeValue{.sweep = 1.0F},
                    AnimationSpec{.timing = SpringTiming{
                                      .mass = 1.0F,
                                      .stiffness = 180.0F,
                                      .damping = 8.0F,
                                      .maximumDuration = 1500ms,
                                  }}));
    composer.Custom(std::make_shared<GaugeProbeElement>(observed), node,
                    "spring-gauge");
  });

  REQUIRE(application->PumpOnce().HasValue());
  clock->AdvanceTime(100ms);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(observed == Catch::Approx(0.619F).margin(0.02F));
  REQUIRE(window->HasActiveAnimations());
  const auto &motion = window->Diagnostics().motion;
  const auto gaugeTrack = std::find_if(
      motion.tracks.begin(), motion.tracks.end(),
      [](const MotionTrackDiagnostics &track) {
        return track.property == GaugeSweep.Id();
      });
  REQUIRE(gaugeTrack != motion.tracks.end());
  CHECK(gaugeTrack->timing.compare("Spring") == 0);

  clock->AdvanceTime(1500ms);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(observed == 1.0F);
  CHECK_FALSE(window->HasActiveAnimations());

  visible = false;
  window->Invalidate(InvalidationKind::Compose);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(window->Diagnostics().motion.tracks.empty());
}

TEST_CASE("target motion retargets from the presented value and becomes idle") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *clock = platform.get();
  auto *recording = renderer.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::move(renderer),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Motion"},
      .title = NGIN::Text::String{"Motion"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  NGIN::F32 target = 1.0F;
  window->SetContent([&](Composer &composer) {
    NodeProperties panel{};
    panel.layout.preferredSize = Size{100.0F, 50.0F};
    panel.layout.horizontalAlignment = HorizontalAlignment::Start;
    panel.layout.verticalAlignment = VerticalAlignment::Start;
    panel.visual.base.background = Color{1.0F, 0.0F, 0.0F, 1.0F};
    panel.motion.opacity = Animate(
        target, AnimationSpec{.timing = TweenTiming{
                                  .duration = 100ms,
                                  .curve = EasingCurve::Linear(),
                              }});
    composer.Border([] {}, panel, "panel");
  });

  REQUIRE(application->PumpOnce().HasValue());
  target = 0.0F;
  window->Invalidate(InvalidationKind::Compose);
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(window->HasActiveAnimations());
  clock->AdvanceTime(50ms);
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(recording->RenderPackets().back().vertices.front().color >> 24U ==
          Catch::Approx(128).margin(2));

  target = 1.0F;
  window->Invalidate(InvalidationKind::Compose);
  REQUIRE(application->PumpOnce().HasValue());
  clock->AdvanceTime(50ms);
  REQUIRE(application->PumpOnce().HasValue());
  const auto alpha =
      recording->RenderPackets().back().vertices.front().color >> 24U;
  CHECK(alpha == Catch::Approx(191).margin(3));
  clock->AdvanceTime(50ms);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(window->HasActiveAnimations());
  CHECK(window->Diagnostics().activeAnimationCount == 0);
}

TEST_CASE("reduced motion settles targets and unmounting removes deadlines") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *clock = platform.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Reduced"},
      .title = NGIN::Text::String{"Reduced"},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  bool visible = true;
  window->SetContent([&](Composer &composer) {
    if (!visible) {
      return;
    }
    NodeProperties panel{};
    panel.motion.translation = AnimateFrom(
        Point{-40.0F, 0.0F}, Point{},
        AnimationSpec{.timing = TweenTiming{
                          .duration = 400ms,
                          .curve = EasingCurve::Linear(),
                      }});
    composer.Border([] {}, panel, "moving");
  });
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(window->HasActiveAnimations());

  visible = false;
  window->Invalidate(InvalidationKind::Compose);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(window->HasActiveAnimations());

  visible = true;
  window->Invalidate(InvalidationKind::Compose);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(window->HasActiveAnimations());

  clock->SetReducedMotion(true);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(window->HasActiveAnimations());
  CHECK(window->Diagnostics().reducedMotion);
}

TEST_CASE("paint transforms also move clipping hit testing and semantics") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *recording = renderer.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::move(renderer),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Transform"},
      .title = NGIN::Text::String{"Transform"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  window->SetContent([&](Composer &composer) {
    NodeProperties panel{};
    panel.layout.preferredSize = Size{50.0F, 40.0F};
    panel.layout.horizontalAlignment = HorizontalAlignment::Start;
    panel.layout.verticalAlignment = VerticalAlignment::Start;
    panel.interaction.focusable = true;
    panel.semantics.role = SemanticRole::Group;
    panel.canvas.clipToBounds = true;
    panel.motion.translation = Animate(
        Point{60.0F, 0.0F},
        AnimationSpec{.timing = TweenTiming{
                          .duration = std::chrono::milliseconds{0},
                      }});
    composer.Canvas(
        [&] {
          NodeProperties child{};
          child.layout.preferredSize = Size{30.0F, 20.0F};
          child.canvasPlacement.offset = Point{40.0F, 0.0F};
          child.canvasPlacement.contributesToDesiredSize = false;
          child.visual.base.background = Color{1.0F, 0.0F, 0.0F, 1.0F};
          composer.Border([] {}, child, "overflowing-child");
        },
        panel, "translated");
  });
  REQUIRE(application->PumpOnce().HasValue());
  const auto handle = window->Tree().Get(window->Tree().Root())->children.front();
  CHECK_FALSE(window->HitTest(Point{10.0F, 10.0F}));
  CHECK(window->HitTest(Point{70.0F, 10.0F}) == handle);
  const auto *semantic = window->Semantics().FindByOwner(
      window->Tree().Get(handle)->id);
  REQUIRE(semantic != nullptr);
  CHECK(semantic->bounds.x == 60.0F);
  REQUIRE_FALSE(recording->RenderPackets().empty());
  const auto &packet = recording->RenderPackets().back();
  REQUIRE_FALSE(packet.batches.empty());
  CHECK(packet.batches.front().scissor == PixelRect{60, 0, 50, 40});
}

TEST_CASE("cancellation finite repetition and window clocks remain independent") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *clock = platform.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto firstResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"FirstMotion"},
      .title = NGIN::Text::String{"First motion"},
  });
  auto secondResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"SecondMotion"},
      .title = NGIN::Text::String{"Second motion"},
  });
  REQUIRE(firstResult.HasValue());
  REQUIRE(secondResult.HasValue());
  auto *first = firstResult.Value();
  auto *second = secondResult.Value();
  AnimationHandle cancellable;
  first->SetContent([&](Composer &composer) {
    NodeProperties node{};
    node.motion.opacity = AnimateFrom(
        0.0F, 1.0F,
        AnimationSpec{.timing = TweenTiming{
                          .duration = 100ms,
                          .curve = EasingCurve::Linear(),
                      }},
        cancellable);
    composer.Border([] {}, node, "first");
  });
  second->SetContent([&](Composer &composer) {
    NodeProperties node{};
    node.motion.translation = AnimateFrom(
        Point{}, Point{100.0F, 0.0F},
        AnimationSpec{
            .timing = TweenTiming{
                .duration = 100ms,
                .curve = EasingCurve::Linear(),
            },
            .repeatCount = 2,
            .repeatMode = AnimationRepeatMode::Reverse,
        });
    composer.Border([] {}, node, "second");
  });
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(first->HasActiveAnimations());
  REQUIRE(second->HasActiveAnimations());
  REQUIRE(first->NextAnimationDeadline().has_value());
  REQUIRE(second->NextAnimationDeadline().has_value());
  CHECK(*first->NextAnimationDeadline() == 16ms);
  CHECK(*second->NextAnimationDeadline() == 16ms);

  clock->AdvanceTime(50ms);
  REQUIRE(application->PumpOnce().HasValue());
  cancellable.Cancel();
  first->Invalidate(InvalidationKind::Paint);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(first->HasActiveAnimations());
  CHECK(second->HasActiveAnimations());

  clock->AdvanceTime(250ms);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(second->HasActiveAnimations());
}

TEST_CASE("theme state and popup transitions use the motion scheduler") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *clock = platform.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"ControlMotion"},
      .title = NGIN::Text::String{"Control motion"},
      .initialSize = PixelSize{320, 180},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  PopupController popup{[window](const InvalidationKind kind) {
    window->Invalidate(kind);
  }};
  window->SetContent([&](Composer &composer) {
    NodeProperties button{};
    button.layout.preferredSize = Size{100.0F, 40.0F};
    button.layout.horizontalAlignment = HorizontalAlignment::Start;
    button.layout.verticalAlignment = VerticalAlignment::Start;
    button.interaction.focusable = true;
    button.visual = MakeButtonVisual(Theme{});
    composer.Button([] {}, button, "button");
    if (popup.IsPresented()) {
      NodeProperties popupProperties{};
      popupProperties.popup.anchor = Rect{0.0F, 0.0F, 100.0F, 40.0F};
      popupProperties.popup.modal = false;
      popupProperties.layout.preferredSize = Size{120.0F, 60.0F};
      PreparePopupMotion(popup, popupProperties);
      composer.Popup([] {}, popupProperties, "popup");
    }
  });
  REQUIRE(application->PumpOnce().HasValue());
  const auto button = window->Tree().Get(window->Tree().Root())->children.front();
  const auto center = Point{20.0F, 20.0F};
  clock->InjectEvent(PointerMoved{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .position = center,
  });
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(window->HasActiveAnimations());
  CHECK(window->HitTest(center) == button);

  clock->AdvanceTime(100ms);
  REQUIRE(application->PumpOnce().HasValue());
  popup.Open();
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(popup.IsOpen());
  CHECK(popup.IsPresented());
  CHECK(window->HasActiveAnimations());

  clock->AdvanceTime(50ms);
  REQUIRE(application->PumpOnce().HasValue());

  popup.Close();
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(popup.IsOpen());
  CHECK(popup.IsPresented());
  const auto popupHandle = window->Tree().Get(window->Tree().Root())->children.back();
  CHECK(window->Tree().Get(popupHandle)->properties.semantics.hidden);
  clock->AdvanceTime(140ms);
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(application->PumpOnce().HasValue());
  CHECK_FALSE(popup.IsPresented());
  CHECK(window->Tree().Get(window->Tree().Root())->children.size() == 1);
}
