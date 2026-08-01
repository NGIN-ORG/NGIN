# NGIN.UI Motion

NGIN.UI animates a value by remembering the value currently on screen and
moving it toward the latest target. Applications change normal state and
compose the new target; they do not run timers or write frame loops.

## Fade, move, scale, and color

Attach animation targets to a keyed element:

```cpp
using namespace std::chrono_literals;

NodeProperties card{};
const AnimationSpec transition{
    .duration = 180ms,
    .easing = Easing::Standard,
};

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
presented value. If `expanded` changes halfway through, the next animation
starts at the value already on screen. Removing the element destroys its
motion state safely.

`AnimateFrom(initial, target, spec)` supplies a first-mount value for entrance
motion. Plain `Animate(target, spec)` presents the first target immediately and
animates later target changes.

The supported target types are:

- `F32` for `value` and `opacity`;
- `Point` for translation and scale;
- `Color` for background, foreground, and border color.

`motion.value` is available to custom controls as
`CustomElementContext::MotionValue()`. `ProgressBar` uses the same public path
for determinate changes and its indeterminate loading motion.

## Curves and repetition

The built-in curves are `Linear`, `Standard`, `EaseIn`, `EaseOut`, and
`EaseInOut`. Durations and optional delays use `std::chrono::milliseconds`.

Set `repeatCount` to a finite number for repeated passes. `Reverse` inserts a
return pass between forward trips and still finishes at the target:

```cpp
AnimationHandle run;

marker.motion.translation = AnimateFrom(
    Point{}, Point{420.0F, 0.0F},
    AnimationSpec{
        .duration = 450ms,
        .easing = Easing::EaseInOut,
        .repeatCount = 3,
        .repeatMode = AnimationRepeatMode::Reverse,
    },
    run);
```

`AnimationHandle` is move-safe. Calling `run.Cancel()` stops the animation at
its presented value. A new handle starts a later run cleanly. A `repeatCount`
of zero repeats while the element remains mounted; use that only for bounded
control states such as an indeterminate progress bar, or keep a handle so the
application can cancel it.

`MotionProperties::onSettled` runs once after the element's explicit targets
finish. Cancellation does not report successful completion.

## Controls and popups

Theme-created buttons and text fields use `Theme::motion` durations for hover,
press, focus, and disabled color changes. Focus outlines fade instead of
appearing abruptly. `ComboBox`, `MenuButton`, and `ContextMenu` keep their
popup mounted for a short fade-and-slide exit, disable its input and semantics
during that exit, and preserve the normal popup focus ownership rules.

No control owns an application timer. The window scheduler requests one next
frame deadline while motion is active and removes that deadline after the last
target settles.

## Reduced motion

`IPlatformBackend::ReducedMotionEnabled()` reports the operating-system
preference. Active targets settle immediately when it is true. The SDL3
backend reads the Windows client-animation preference; other platforms report
the capability only when they can supply it.

`Window::SetMotionEnabled(false)` lets an application offer an additional
less-motion setting. It can reduce motion further but cannot override a system
request for reduced motion. Re-enable it with `SetMotionEnabled(true)`.

## Transform behavior

Translation and scale are paint transforms. They do not cause measurement or
arrangement and therefore do not move neighboring layout. Scale uses the
element center as its origin.

The transformed geometry is used consistently for:

- painting the element and its descendants;
- clips created inside that subtree;
- pointer hit testing;
- custom-control local pointer coordinates;
- semantic and native accessibility bounds.

Rotation, animated layout, keyframes, sequences, and shared-element
transitions are not part of this API.

## Deterministic tests

`Testing::TestPlatformBackend` owns the monotonic test clock:

```cpp
auto *clock = platform.get();
// Create the application and start a target change.
clock->AdvanceTime(90ms);
REQUIRE(application->PumpOnce().HasValue());
```

Tests can inspect `Window::HasActiveAnimations()`,
`Window::NextAnimationDeadline()`, and the motion fields in
`WindowDiagnostics`. `TestPlatformBackend::SetReducedMotion(true)` exercises
the immediate-settle path without relying on a machine setting.

The Gallery's **Motion** page is the runnable reference for interruption,
easing, finite repetition, cancellation, popup motion, progress motion, and
the less-motion preview.
