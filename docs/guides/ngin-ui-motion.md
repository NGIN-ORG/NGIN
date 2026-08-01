# NGIN.UI Motion

NGIN.UI moves a property from the value currently on screen to a new target.
Change normal application state and compose the new target; the window owns the
clock and stops requesting frames when every track is idle.

## Fade, move, scale, and color

Attach targets to a keyed element:

```cpp
using namespace std::chrono_literals;

const AnimationSpec transition{
    .timing = TweenTiming{
        .duration = 180ms,
        .curve = EasingCurve::Standard(),
    },
};

NodeProperties card{};
card.motion.opacity = Animate(expanded ? 1.0F : 0.35F, transition);
card.motion.translation =
    Animate(expanded ? Point{180.0F, 0.0F} : Point{}, transition);
card.motion.scale = Animate(expanded ? Point{1.0F, 1.0F}
                                     : Point{0.9F, 0.9F},
                            transition);
card.motion.background =
    Animate(expanded ? theme.colors.accentHovered
                     : theme.colors.accentPressed,
            transition);

composer.Border([] {}, card, "moving-card");
```

The key is the animation identity. Reusing `"moving-card"` preserves the
presented value, so a target changed halfway through starts from what the user
can already see. Removing the element destroys its tracks and deadlines.

`AnimateFrom(initial, target, spec)` supplies a first-mount entrance value.
`Animate(target, spec)` presents the first target immediately and animates later
changes.

## Curves

Built-in curves are allocation-free values:

```cpp
auto linear = EasingCurve::Linear();
auto standard = EasingCurve::Standard();
auto easeIn = EasingCurve::EaseIn();
auto easeOut = EasingCurve::EaseOut();
auto easeInOut = EasingCurve::EaseInOut();
auto bezier = EasingCurve::CubicBezier(0.2F, 0.0F, 0.2F, 1.0F);
auto stepped = EasingCurve::Steps(5, StepPosition::End);
```

A custom curve is an immutable shared object:

```cpp
class OvershootCurve final : public IEasingCurve {
public:
  auto Evaluate(F32 progress) const -> F32 override {
    return progress * 1.15F;
  }

  auto Name() const noexcept -> std::string_view override {
    return "App.Overshoot";
  }
};

const auto overshoot = EasingCurve::MakeCustom<OvershootCurve>();
```

`Evaluate` receives a finite value clamped to `[0, 1]`. A finite result may be
outside that range. This lets a transform or application value overshoot.
Opacity and color properties apply their own safe output limits. A thrown
exception or non-finite result is contained and falls back to linear progress;
the failure is counted in motion diagnostics.

The runtime owns custom curves through `shared_ptr<const IEasingCurve>`. Treat
an implementation as immutable and safe for concurrent `const` reads. Curve
identity is shared-object identity, so retain and reuse one `EasingCurve` value
instead of constructing a new custom object during every composition.

## Tween and spring timing

A tween has a duration and curve. A spring has physical parameters and settles
when both displacement and velocity reach their rest thresholds:

```cpp
marker.motion.translation = Animate(
    moved ? Point{420.0F, 0.0F} : Point{},
    AnimationSpec{
        .timing = SpringTiming{
            .mass = 1.0F,
            .stiffness = 180.0F,
            .damping = 10.0F,
        },
    });
```

`maximumDuration` bounds malformed or very soft springs. Springs may overshoot;
they are not converted into normalized easing curves.

Set `repeatCount` for repeated passes. `Reverse` inserts a return pass between
forward trips:

```cpp
AnimationHandle run;

marker.motion.translation = AnimateFrom(
    Point{}, Point{420.0F, 0.0F},
    AnimationSpec{
        .timing = TweenTiming{
            .duration = 450ms,
            .curve = EasingCurve::EaseInOut(),
        },
        .repeatCount = 3,
        .repeatMode = AnimationRepeatMode::Reverse,
    },
    run);
```

`run.Cancel()` stops at the presented value. A `repeatCount` of zero repeats
while mounted; keep a handle for explicitly started indefinite motion.
`MotionProperties::onSettled` runs once when explicit targets finish.

## Custom value types and properties

Specialize the public interpolator for an application value:

```cpp
struct GaugeReading final {
  F32 sweep{0.0F};
  auto operator<=>(const GaugeReading &) const noexcept = default;
};

template <>
struct NGIN::UI::AnimationInterpolator<GaugeReading> final {
  static auto Interpolate(GaugeReading start, GaugeReading end,
                          F32 progress) noexcept -> GaugeReading {
    return {.sweep = start.sweep + (end.sweep - start.sweep) * progress};
  }
};
```

Give the property a globally qualified, static-lifetime name, declare its
target, and read it from a custom control:

```cpp
inline const AnimationProperty<GaugeReading> GaugeSweep{
    "Acme.Gauge.Sweep", GaugeReading{}};

NodeProperties gauge{};
gauge.motion.Set(
    GaugeSweep,
    Animate(GaugeReading{.sweep = expanded ? 1.0F : 0.0F}, transition));
composer.Custom(gaugeElement, gauge, "gauge");

// Inside ICustomElement::Paint:
const auto reading = context.MotionValue(GaugeSweep);
const auto moving = context.IsMotionActive(GaugeSweep);
```

Property identity is the stable hash of its name. The runtime also checks the
name and value type; conflicting declarations are rejected and counted in
diagnostics. Built-in `motion.opacity`, `translation`, `scale`, `background`,
`foreground`, `borderColor`, and scalar `value` are conveniences over this same
track engine. `CustomElementContext::MotionValue()` remains the short form for
the built-in scalar value.

`AnimationValuePolicy<T>` is the optional customization point for a type that
needs output constraints. Custom properties are unbounded by default.

## Wait for motion

Use a `MotionController` when the next action must wait for an animation. Keep
the controller with your view model and attach it to the same keyed element on
every composition:

```cpp
MotionController cardMotion;

window.SetContent([&](Composer& composer) {
  NodeProperties card{};
  cardMotion.Attach(card.motion);
  composer.Border([] {}, card, "account-card");
});
```

Create a UI task context from the application. Start a task with the normal
NGIN async API:

```cpp
auto uiContext = application.CreateTaskContext();
auto fade = NGIN::Async::Spawn(
    uiContext,
    cardMotion.FadeToAsync(uiContext, 0.25F, transition));
```

`AnimateToAsync` works with every `AnimationProperty<T>` accepted by the
declarative engine. `FadeToAsync`, `TranslateToAsync`, `ScaleToAsync`, and
`ColorToAsync` are shorter names for common properties.

Write steps in their reading order:

```cpp
auto ShowCard(NGIN::Async::TaskContext& context,
              MotionController& motion) -> NGIN::Async::Task<void> {
  co_await motion.FadeToAsync(context, 1.0F, transition);
  co_await motion.TranslateToAsync(context, Point{}, transition);
}
```

Start operations together with the normal async combinators:

```cpp
auto results = co_await NGIN::Async::WhenAll(
    context,
    motion.FadeToAsync(context, 1.0F, transition),
    motion.ScaleToAsync(context, Point{1.0F, 1.0F}, transition));

auto first = co_await NGIN::Async::WhenAny(
    context,
    left.TranslateToAsync(context, Point{240.0F, 0.0F}, transition),
    right.TranslateToAsync(context, Point{240.0F, 0.0F}, transition));
```

An awaited operation returns `MotionOutcome::Completed`, `Canceled`,
`Interrupted`, or `Unmounted`. A newer controller target for the same property
interrupts the older waiter. A cancellation token passed to
`Application::CreateTaskContext` stops its active motion. Removing the element,
closing its window, or shutting down the application releases its waiter.
Continuations always run through the UI scheduler, never inside painting.

Declarative targets own a property when both APIs name it. The controller
operation reports `Interrupted`; this prevents two writers from silently
changing the same value. Use declarative targets for state-driven transitions.
Use a controller when the order or result is part of application behavior.

Reduced motion uses the same code. The final value is presented immediately
and the waiter resumes on the next UI-scheduler turn.

## Diagnostics and deterministic tests

`Window::Diagnostics().motion.tracks` reports the owner, property identity and
name, interpolated value and interpolator type, timing type, curve name,
custom-curve flag, active state, and evaluation-failure count. Property
collisions are reported
by `propertyConflictCount`.

`Testing::TestPlatformBackend` owns the monotonic test clock:

```cpp
auto *clock = platform.get();
clock->AdvanceTime(90ms);
REQUIRE(application->PumpOnce().HasValue());
```

Tests can also inspect `Window::HasActiveAnimations()` and
`Window::NextAnimationDeadline()`. The Gallery **Motion** page demonstrates
built-in and custom curves, an editable cubic Bézier, spring timing, a custom
control property, awaited sequences, parallel motion, interruption,
cancellation, and reduced motion.

## Reduced motion and transforms

The operating-system preference and `Window::SetMotionEnabled(false)` settle
active targets immediately. An application setting can reduce motion further
but cannot override an operating-system request.

Translation and scale happen after layout. The same transformed geometry is
used for painting, clips, pointer hit testing, custom-control coordinates,
semantics, and native accessibility bounds.

## Migration from the closed motion API

Milestone 23 intentionally replaces the old contract; there is no second
legacy engine.

- Replace `Easing::Standard` with `EasingCurve::Standard()` and the equivalent
  factory for other built-ins.
- Replace `AnimationSpec{.duration = ..., .easing = ...}` with
  `AnimationSpec{.timing = TweenTiming{.duration = ..., .curve = ...}}`.
- Use `SpringTiming` directly for physics motion.
- Specialize `AnimationInterpolator<T>` and use `AnimationProperty<T>` for
  application-owned values instead of adding runtime fields.
- Read a custom property with `context.MotionValue(property)`.

Retargeting, cancellation, reduced motion, element lifetime, and the window
scheduler are unchanged because built-in and custom properties use one engine.
