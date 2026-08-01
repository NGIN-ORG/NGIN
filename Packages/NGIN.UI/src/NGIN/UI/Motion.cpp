#include <NGIN/UI/Animation.hpp>

#include "MotionInternal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace NGIN::UI {
AnimationHandle::AnimationHandle()
    : m_cancellation(std::make_shared<Detail::AnimationCancellation>()) {}

void AnimationHandle::Cancel() noexcept {
  if (m_cancellation) {
    m_cancellation->cancelled.store(true, std::memory_order_relaxed);
  }
}

auto AnimationHandle::IsCancelled() const noexcept -> bool {
  return m_cancellation != nullptr &&
         m_cancellation->cancelled.load(std::memory_order_relaxed);
}
} // namespace NGIN::UI

namespace NGIN::UI::Detail {
namespace {
constexpr UInt32 MaximumFiniteRepeatCount = 10'000;
constexpr auto MaximumDuration = std::chrono::minutes{1};
constexpr auto SpringSampleStep = std::chrono::milliseconds{4};
constexpr UInt32 SpringRestSamples = 8;

class ValueStorage final {
public:
  ValueStorage(const AnimationValueOperations &operations, const void *source)
      : m_operations(&operations),
        m_value(::operator new(operations.size,
                               std::align_val_t{operations.alignment})) {
    if (!m_operations->constructCopy(m_value, source)) {
      ::operator delete(m_value, std::align_val_t{m_operations->alignment});
      m_value = nullptr;
      throw std::runtime_error{"Animation value construction failed"};
    }
  }

  ValueStorage(const ValueStorage &) = delete;
  ValueStorage(ValueStorage &&other) noexcept
      : m_operations(std::exchange(other.m_operations, nullptr)),
        m_value(std::exchange(other.m_value, nullptr)) {}
  auto operator=(const ValueStorage &) -> ValueStorage & = delete;
  auto operator=(ValueStorage &&other) noexcept -> ValueStorage & {
    if (this != &other) {
      Reset();
      m_operations = std::exchange(other.m_operations, nullptr);
      m_value = std::exchange(other.m_value, nullptr);
    }
    return *this;
  }
  ~ValueStorage() { Reset(); }

  [[nodiscard]] auto Get() noexcept -> void * { return m_value; }
  [[nodiscard]] auto Get() const noexcept -> const void * { return m_value; }

  [[nodiscard]] auto Assign(const void *source) noexcept -> bool {
    return m_operations->assignCopy(m_value, source);
  }

  [[nodiscard]] auto Equals(const void *other) const noexcept -> bool {
    return m_operations->equals(m_value, other);
  }

  [[nodiscard]] auto Interpolate(const void *start, const void *end,
                                 const F32 progress) noexcept -> bool {
    return m_operations->interpolate(start, end, progress, m_value);
  }

  void Apply(const AnimationOutputPolicy policy) noexcept {
    m_operations->applyPolicy(m_value, policy);
  }

private:
  void Reset() noexcept {
    if (m_value == nullptr || m_operations == nullptr) {
      return;
    }
    m_operations->destroy(m_value);
    ::operator delete(m_value, std::align_val_t{m_operations->alignment});
    m_value = nullptr;
    m_operations = nullptr;
  }

  const AnimationValueOperations *m_operations{nullptr};
  void *m_value{nullptr};
};

struct SpringSample final {
  F32 progress{1.0F};
  F32 velocity{0.0F};
  bool valid{true};
};

[[nodiscard]] auto NormalizeSpring(SpringTiming spring) noexcept
    -> SpringTiming {
  spring.mass = std::isfinite(spring.mass) ? std::max(0.001F, spring.mass)
                                           : 1.0F;
  spring.stiffness = std::isfinite(spring.stiffness)
                         ? std::max(0.001F, spring.stiffness)
                         : 220.0F;
  spring.damping = std::isfinite(spring.damping)
                       ? std::max(0.0F, spring.damping)
                       : 20.0F;
  spring.initialVelocity = std::isfinite(spring.initialVelocity)
                               ? spring.initialVelocity
                               : 0.0F;
  spring.restDisplacement = std::isfinite(spring.restDisplacement)
                                ? std::max(0.000001F,
                                           spring.restDisplacement)
                                : 0.001F;
  spring.restVelocity = std::isfinite(spring.restVelocity)
                            ? std::max(0.000001F, spring.restVelocity)
                            : 0.001F;
  spring.maximumDuration =
      std::clamp(spring.maximumDuration, std::chrono::milliseconds{0},
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     MaximumDuration));
  return spring;
}

[[nodiscard]] auto SampleSpring(const SpringTiming &spring,
                                const F64 seconds) noexcept -> SpringSample {
  if (seconds <= 0.0) {
    return SpringSample{.progress = 0.0F,
                        .velocity = spring.initialVelocity};
  }
  const auto mass = static_cast<F64>(spring.mass);
  const auto stiffness = static_cast<F64>(spring.stiffness);
  const auto damping = static_cast<F64>(spring.damping);
  const auto initialVelocity = static_cast<F64>(spring.initialVelocity);
  const auto omega = std::sqrt(stiffness / mass);
  const auto zeta = damping / (2.0 * std::sqrt(stiffness * mass));
  constexpr F64 initialDisplacement = -1.0;

  F64 displacement = initialDisplacement;
  F64 velocity = initialVelocity;
  if (zeta < 1.0 - 0.000001) {
    const auto damped = omega * std::sqrt(1.0 - zeta * zeta);
    const auto a = initialDisplacement;
    const auto b = (initialVelocity + zeta * omega * a) / damped;
    const auto decay = std::exp(-zeta * omega * seconds);
    const auto cosine = std::cos(damped * seconds);
    const auto sine = std::sin(damped * seconds);
    displacement = decay * (a * cosine + b * sine);
    velocity = decay *
               ((-zeta * omega) * (a * cosine + b * sine) +
                (-a * damped * sine + b * damped * cosine));
  } else if (zeta <= 1.0 + 0.000001) {
    const auto a = initialDisplacement;
    const auto b = initialVelocity + omega * a;
    const auto decay = std::exp(-omega * seconds);
    displacement = (a + b * seconds) * decay;
    velocity = (b - omega * (a + b * seconds)) * decay;
  } else {
    const auto root = std::sqrt(zeta * zeta - 1.0);
    const auto first = -omega * (zeta - root);
    const auto second = -omega * (zeta + root);
    const auto firstWeight =
        (initialVelocity - second * initialDisplacement) / (first - second);
    const auto secondWeight = initialDisplacement - firstWeight;
    const auto firstDecay = std::exp(first * seconds);
    const auto secondDecay = std::exp(second * seconds);
    displacement = firstWeight * firstDecay + secondWeight * secondDecay;
    velocity = first * firstWeight * firstDecay +
               second * secondWeight * secondDecay;
  }

  const auto progress = 1.0 + displacement;
  if (!std::isfinite(progress) || !std::isfinite(velocity)) {
    return SpringSample{.valid = false};
  }
  return SpringSample{.progress = static_cast<F32>(progress),
                      .velocity = static_cast<F32>(velocity)};
}

[[nodiscard]] auto ResolveSpringDuration(const SpringTiming &spring) noexcept
    -> MonotonicTime {
  const auto maximum =
      std::chrono::duration_cast<MonotonicTime>(spring.maximumDuration);
  if (maximum.count() <= 0) {
    return {};
  }
  const auto step = std::chrono::duration_cast<MonotonicTime>(SpringSampleStep);
  UInt32 atRest = 0;
  for (auto elapsed = step; elapsed <= maximum; elapsed += step) {
    const auto sample = SampleSpring(
        spring, std::chrono::duration<F64>(elapsed).count());
    if (sample.valid &&
        std::abs(1.0F - sample.progress) <= spring.restDisplacement &&
        std::abs(sample.velocity) <= spring.restVelocity) {
      ++atRest;
      if (atRest >= SpringRestSamples) {
        return elapsed;
      }
    } else {
      atRest = 0;
    }
  }
  return maximum;
}

[[nodiscard]] auto Normalize(AnimationSpec spec) noexcept -> AnimationSpec {
  spec.delay = std::clamp(spec.delay, std::chrono::milliseconds{0},
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              MaximumDuration));
  if (spec.repeatCount != 0) {
    spec.repeatCount = std::clamp(spec.repeatCount, UInt32{1},
                                  MaximumFiniteRepeatCount);
  }
  std::visit(
      [](auto &timing) {
        using T = std::decay_t<decltype(timing)>;
        if constexpr (std::is_same_v<T, TweenTiming>) {
          timing.duration =
              std::clamp(timing.duration, std::chrono::milliseconds{0},
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             MaximumDuration));
        } else {
          timing = NormalizeSpring(timing);
        }
      },
      spec.timing);
  return spec;
}

[[nodiscard]] auto ResolveDuration(const AnimationTiming &timing) noexcept
    -> MonotonicTime {
  return std::visit(
      [](const auto &value) -> MonotonicTime {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, TweenTiming>) {
          return std::chrono::duration_cast<MonotonicTime>(value.duration);
        } else {
          return ResolveSpringDuration(value);
        }
      },
      timing);
}

struct TimingSample final {
  F32 progress{1.0F};
  bool valid{true};
};

[[nodiscard]] auto SampleTiming(const AnimationTiming &timing,
                                const MonotonicTime elapsed,
                                const MonotonicTime duration) noexcept
    -> TimingSample {
  if (duration.count() <= 0 || elapsed >= duration) {
    return {};
  }
  return std::visit(
      [&](const auto &value) -> TimingSample {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, TweenTiming>) {
          const auto linear = static_cast<F32>(
              static_cast<F64>(elapsed.count()) /
              static_cast<F64>(duration.count()));
          bool valid = true;
          const auto progress =
              EvaluateEasingCurve(value.curve, linear, valid);
          return TimingSample{.progress = progress, .valid = valid};
        } else {
          const auto sample = SampleSpring(
              value, std::chrono::duration<F64>(elapsed).count());
          return TimingSample{.progress = sample.progress,
                              .valid = sample.valid};
        }
      },
      timing);
}

[[nodiscard]] auto TimingName(const AnimationTiming &timing) noexcept
    -> std::string_view {
  return std::holds_alternative<TweenTiming>(timing) ? "Tween" : "Spring";
}

[[nodiscard]] auto CurveName(const AnimationTiming &timing) noexcept
    -> std::string_view {
  if (const auto *tween = std::get_if<TweenTiming>(&timing)) {
    return tween->curve.Name();
  }
  return "Spring";
}

[[nodiscard]] auto CustomCurve(const AnimationTiming &timing) noexcept -> bool {
  const auto *tween = std::get_if<TweenTiming>(&timing);
  return tween != nullptr && tween->curve.IsCustom();
}

struct Track final {
  Track(const AnimationDeclarationView &declaration, const MonotonicTime now,
        const bool reducedMotion, bool &explicitMotionStarted)
      : property(declaration.property), propertyName(declaration.propertyName),
        outputPolicy(declaration.outputPolicy),
        operations(declaration.operations), spec(Normalize(*declaration.spec)),
        duration(ResolveDuration(spec.timing)), started(now),
        handleIdentity(declaration.handleIdentity), builtIn(declaration.builtIn),
        value(*operations,
              declaration.initial != nullptr ? declaration.initial
                                             : declaration.target),
        start(*operations,
              declaration.initial != nullptr ? declaration.initial
                                             : declaration.target),
        target(*operations, declaration.target),
        candidate(*operations,
                  declaration.initial != nullptr ? declaration.initial
                                                 : declaration.target) {
    value.Apply(outputPolicy);
    start.Apply(outputPolicy);
    const auto different = !start.Equals(target.Get());
    active = !declaration.cancelled && !reducedMotion && different &&
             duration.count() > 0;
    if (!active && !declaration.cancelled) {
      static_cast<void>(value.Assign(target.Get()));
      value.Apply(outputPolicy);
    }
    explicitMotionStarted = explicitMotionStarted || different;
  }

  Track(const Track &) = delete;
  Track(Track &&) noexcept = default;
  auto operator=(const Track &) -> Track & = delete;
  auto operator=(Track &&) noexcept -> Track & = default;

  [[nodiscard]] auto Matches(const AnimationDeclarationView &declaration) const
      noexcept -> bool {
    return property == declaration.property && operations == declaration.operations &&
           propertyName == declaration.propertyName;
  }

  [[nodiscard]] auto DeclarationChanged(
      const AnimationDeclarationView &declaration) const noexcept -> bool {
    return !target.Equals(declaration.target) ||
           spec != Normalize(*declaration.spec) ||
           handleIdentity != declaration.handleIdentity ||
           outputPolicy != declaration.outputPolicy;
  }

  auto Synchronize(const AnimationDeclarationView &declaration,
                   const MonotonicTime now, const bool reducedMotion,
                   const bool restartRepeating,
                   bool &explicitMotionStarted) noexcept -> bool {
    seen = true;
    if (!Matches(declaration)) {
      ++conflictCount;
      return false;
    }
    if (!DeclarationChanged(declaration)) {
      if (declaration.cancelled) {
        active = false;
      } else if (restartRepeating && spec.repeatCount == 0 &&
                 declaration.initial != nullptr) {
        static_cast<void>(start.Assign(declaration.initial));
        start.Apply(outputPolicy);
        static_cast<void>(value.Assign(start.Get()));
        started = now;
        active = !reducedMotion && !start.Equals(target.Get()) &&
                 duration.count() > 0;
      }
      return false;
    }

    const auto changedBefore = !value.Equals(declaration.target);
    static_cast<void>(start.Assign(value.Get()));
    static_cast<void>(target.Assign(declaration.target));
    spec = Normalize(*declaration.spec);
    duration = ResolveDuration(spec.timing);
    started = now;
    handleIdentity = declaration.handleIdentity;
    outputPolicy = declaration.outputPolicy;
    if (declaration.cancelled) {
      active = false;
      return false;
    }
    active = !reducedMotion && !start.Equals(target.Get()) &&
             duration.count() > 0;
    if (!active) {
      static_cast<void>(value.Assign(target.Get()));
      value.Apply(outputPolicy);
    }
    explicitMotionStarted = explicitMotionStarted || changedBefore;
    return true;
  }

  auto Advance(const MonotonicTime now) noexcept -> bool {
    if (!active) {
      return false;
    }
    const auto delay =
        std::chrono::duration_cast<MonotonicTime>(spec.delay);
    if (now <= started + delay) {
      static_cast<void>(candidate.Assign(start.Get()));
      candidate.Apply(outputPolicy);
      return PresentCandidate();
    }
    if (duration.count() <= 0) {
      return Finish();
    }

    const auto elapsed = now - started - delay;
    const auto leg = static_cast<UInt64>(elapsed / duration);
    const auto finite = spec.repeatCount != 0;
    UInt64 legCount = std::numeric_limits<UInt64>::max();
    if (finite) {
      legCount = spec.repeatMode == AnimationRepeatMode::Reverse
                     ? static_cast<UInt64>(spec.repeatCount) * 2U - 1U
                     : static_cast<UInt64>(spec.repeatCount);
      if (leg >= legCount) {
        return Finish();
      }
    }

    const auto sample = SampleTiming(spec.timing, elapsed % duration, duration);
    if (!sample.valid) {
      ++evaluationFailureCount;
    }
    auto progress = sample.progress;
    if (spec.repeatMode == AnimationRepeatMode::Reverse &&
        (leg % 2U) != 0U) {
      progress = 1.0F - progress;
    }
    if (!candidate.Interpolate(start.Get(), target.Get(), progress)) {
      ++evaluationFailureCount;
      static_cast<void>(candidate.Assign(target.Get()));
    }
    candidate.Apply(outputPolicy);
    return PresentCandidate();
  }

  auto Settle() noexcept -> bool {
    active = false;
    static_cast<void>(candidate.Assign(target.Get()));
    candidate.Apply(outputPolicy);
    return PresentCandidate();
  }

  [[nodiscard]] auto NextDeadline(const MonotonicTime now) const noexcept
      -> std::optional<MonotonicTime> {
    if (!active) {
      return std::nullopt;
    }
    const auto delayEnd =
        started + std::chrono::duration_cast<MonotonicTime>(spec.delay);
    if (now < delayEnd) {
      return delayEnd;
    }
    auto deadline = now + std::chrono::milliseconds{16};
    if (spec.repeatCount != 0) {
      const auto legs = spec.repeatMode == AnimationRepeatMode::Reverse
                            ? static_cast<UInt64>(spec.repeatCount) * 2U - 1U
                            : static_cast<UInt64>(spec.repeatCount);
      const auto end = delayEnd +
                       duration * static_cast<MonotonicTime::rep>(legs);
      deadline = std::min(deadline, end);
    }
    return deadline;
  }

  [[nodiscard]] auto Finish() noexcept -> bool {
    active = false;
    static_cast<void>(candidate.Assign(target.Get()));
    candidate.Apply(outputPolicy);
    return PresentCandidate();
  }

  [[nodiscard]] auto PresentCandidate() noexcept -> bool {
    const auto changed = !value.Equals(candidate.Get());
    if (changed && !value.Assign(candidate.Get())) {
      ++evaluationFailureCount;
      return false;
    }
    return changed;
  }

  AnimationPropertyId property{};
  std::string_view propertyName{};
  AnimationOutputPolicy outputPolicy{AnimationOutputPolicy::Unbounded};
  const AnimationValueOperations *operations{nullptr};
  AnimationSpec spec{};
  MonotonicTime duration{};
  MonotonicTime started{};
  const void *handleIdentity{nullptr};
  bool builtIn{false};
  bool active{false};
  bool seen{false};
  UInt64 evaluationFailureCount{0};
  UInt64 conflictCount{0};
  ValueStorage value;
  ValueStorage start;
  ValueStorage target;
  ValueStorage candidate;
};

[[nodiscard]] auto VisualStateFor(const RuntimeNode &node) noexcept
    -> VisualStateFlags {
  auto state = node.properties.visual.state;
  if (node.interaction.hovered) {
    state |= VisualStateFlags::Hovered;
  }
  if (node.interaction.pressed || node.interaction.keyboardPressed) {
    state |= VisualStateFlags::Pressed;
  }
  if (node.interaction.focused) {
    state |= VisualStateFlags::Focused;
  }
  if (!node.properties.interaction.enabled) {
    state |= VisualStateFlags::Disabled;
  }
  if ((node.type == ElementType::TextField ||
       node.type == ElementType::TextArea) &&
      node.properties.textField.readOnly) {
    state |= VisualStateFlags::ReadOnly;
  }
  return state;
}
} // namespace

struct MotionState final {
  std::vector<Track> tracks{};
  bool hasBackground{false};
  bool hasForeground{false};
  bool hasBorderColor{false};
  bool completionPending{false};
  bool reducedMotion{false};
  bool reducedMotionInitialized{false};
  UInt64 propertyConflictCount{0};
};

namespace {
[[nodiscard]] auto FindTrack(MotionState &state,
                             const AnimationPropertyId property) noexcept
    -> Track * {
  const auto found = std::find_if(
      state.tracks.begin(), state.tracks.end(),
      [property](const Track &track) { return track.property == property; });
  return found == state.tracks.end() ? nullptr : &*found;
}

[[nodiscard]] auto FindTrack(const MotionState &state,
                             const AnimationPropertyId property) noexcept
    -> const Track * {
  const auto found = std::find_if(
      state.tracks.begin(), state.tracks.end(),
      [property](const Track &track) { return track.property == property; });
  return found == state.tracks.end() ? nullptr : &*found;
}

auto Synchronize(MotionState &state,
                 const AnimationDeclarationView &declaration,
                 const MonotonicTime now, const bool reducedMotion,
                 const bool restartRepeating, bool &explicitMotionStarted,
                 bool &changed) -> void {
  if (declaration.operations == nullptr || declaration.target == nullptr ||
      declaration.spec == nullptr || declaration.property.value == 0) {
    ++state.propertyConflictCount;
    return;
  }
  auto *track = FindTrack(state, declaration.property);
  if (track == nullptr) {
    state.tracks.emplace_back(declaration, now, reducedMotion,
                              explicitMotionStarted);
    state.tracks.back().seen = true;
    changed = true;
    return;
  }
  if (!track->Matches(declaration) ||
      (track->builtIn && !declaration.builtIn)) {
    ++state.propertyConflictCount;
    track->seen = true;
    return;
  }
  changed |= track->Synchronize(declaration, now, reducedMotion,
                                restartRepeating, explicitMotionStarted);
}

template <AnimatableValue T>
auto ViewFor(const AnimationProperty<T> &property,
             const AnimationTarget<T> &target) noexcept
    -> AnimationDeclarationView {
  return AnimationDeclarationView{
      .property = property.Id(),
      .propertyName = property.Name(),
      .outputPolicy = property.OutputPolicy(),
      .operations = &AnimationValueOperationsFor<T>(),
      .target = &target.Target(),
      .initial = target.Initial() ? &*target.Initial() : nullptr,
      .spec = &target.Spec(),
      .handleIdentity = target.HandleIdentity(),
      .cancelled = target.IsCancelled(),
      .builtIn = property.IsBuiltIn(),
  };
}

template <AnimatableValue T>
auto ImmediateTarget(const T &value) -> AnimationTarget<T> {
  return Animate(value, AnimationSpec{.timing = TweenTiming{
                                          .duration =
                                              std::chrono::milliseconds{0},
                                      }});
}

template <AnimatableValue T>
void SynchronizeOptional(MotionState &state,
                         const AnimationProperty<T> &property,
                         const std::optional<AnimationTarget<T>> &declaration,
                         const T &fallback, const MonotonicTime now,
                         const bool reducedMotion, const bool restartRepeating,
                         bool &explicitMotionStarted, bool &changed) {
  if (declaration) {
    Synchronize(state, ViewFor(property, *declaration), now, reducedMotion,
                restartRepeating, explicitMotionStarted, changed);
    return;
  }
  if (FindTrack(state, property.Id()) != nullptr) {
    const auto target = ImmediateTarget(fallback);
    auto ignored = false;
    Synchronize(state, ViewFor(property, target), now, reducedMotion, false,
                ignored, changed);
  }
}

template <AnimatableValue T>
void SynchronizeAutomatic(MotionState &state,
                          const AnimationProperty<T> &property,
                          const std::optional<T> &value,
                          const AnimationSpec &transition,
                          const MonotonicTime now, const bool reducedMotion,
                          bool &changed) {
  if (value) {
    auto target = Animate(*value, transition);
    auto ignored = false;
    Synchronize(state, ViewFor(property, target), now, reducedMotion, false,
                ignored, changed);
    return;
  }
  if (FindTrack(state, property.Id()) != nullptr) {
    const auto target = ImmediateTarget(property.DefaultValue());
    auto ignored = false;
    Synchronize(state, ViewFor(property, target), now, reducedMotion, false,
                ignored, changed);
  }
}

auto AdvanceNode(RuntimeNode &node, const MonotonicTime now,
                 const bool reducedMotion) -> MotionFrameResult {
  const auto &properties = node.properties.motion;
  const auto stateFlags = VisualStateFor(node);
  const auto style = ResolveVisualStyle(node.properties.visual, stateFlags);
  const auto needsState = node.motion || properties.value || properties.opacity ||
                          properties.translation || properties.scale ||
                          properties.background || properties.foreground ||
                          properties.borderColor || !properties.Bindings().empty() ||
                          node.properties.visual.transition != AnimationSpec{
                              .timing = TweenTiming{
                                  .duration = std::chrono::milliseconds{0},
                              }};
  if (!needsState) {
    return {};
  }
  if (!node.motion) {
    node.motion = std::make_shared<MotionState>();
  }
  auto &state = *node.motion;
  MotionFrameResult result{};
  const auto motionRestored = state.reducedMotionInitialized &&
                              state.reducedMotion && !reducedMotion;
  state.reducedMotion = reducedMotion;
  state.reducedMotionInitialized = true;
  for (auto &track : state.tracks) {
    track.seen = false;
    result.changed |= track.Advance(now);
    if (reducedMotion) {
      result.changed |= track.Settle();
    }
  }

  bool explicitMotionStarted = false;
  SynchronizeOptional(state, MotionProperty::Value, properties.value, 0.0F, now,
                      reducedMotion, motionRestored, explicitMotionStarted,
                      result.changed);
  SynchronizeOptional(state, MotionProperty::Opacity, properties.opacity, 1.0F,
                      now, reducedMotion, motionRestored,
                      explicitMotionStarted, result.changed);
  SynchronizeOptional(state, MotionProperty::Translation,
                      properties.translation, Point{}, now, reducedMotion,
                      motionRestored, explicitMotionStarted, result.changed);
  SynchronizeOptional(state, MotionProperty::Scale, properties.scale,
                      Point{1.0F, 1.0F}, now, reducedMotion, motionRestored,
                      explicitMotionStarted, result.changed);

  auto resolvedStyle = style;
  if (!resolvedStyle.background && node.properties.paintsBackground) {
    resolvedStyle.background = node.properties.background;
  }
  if (properties.background) {
    Synchronize(state, ViewFor(MotionProperty::Background,
                               *properties.background),
                now, reducedMotion, motionRestored, explicitMotionStarted,
                result.changed);
    state.hasBackground = true;
  } else {
    SynchronizeAutomatic(state, MotionProperty::Background,
                         resolvedStyle.background,
                         node.properties.visual.transition, now, reducedMotion,
                         result.changed);
    state.hasBackground = resolvedStyle.background.has_value();
  }
  if (properties.foreground) {
    Synchronize(state,
                ViewFor(MotionProperty::Foreground, *properties.foreground),
                now, reducedMotion, motionRestored, explicitMotionStarted,
                result.changed);
    state.hasForeground = true;
  } else {
    SynchronizeAutomatic(state, MotionProperty::Foreground,
                         resolvedStyle.foreground,
                         node.properties.visual.transition, now, reducedMotion,
                         result.changed);
    state.hasForeground = resolvedStyle.foreground.has_value();
  }
  if (properties.borderColor) {
    Synchronize(state,
                ViewFor(MotionProperty::BorderColor, *properties.borderColor),
                now, reducedMotion, motionRestored, explicitMotionStarted,
                result.changed);
    state.hasBorderColor = true;
  } else {
    SynchronizeAutomatic(state, MotionProperty::BorderColor,
                         resolvedStyle.borderColor,
                         node.properties.visual.transition, now, reducedMotion,
                         result.changed);
    state.hasBorderColor = resolvedStyle.borderColor.has_value();
  }
  SynchronizeAutomatic(
      state, MotionProperty::FocusOpacity,
      std::optional<F32>{HasVisualState(stateFlags, VisualStateFlags::Focused)
                             ? 1.0F
                             : 0.0F},
      node.properties.visual.transition, now, reducedMotion, result.changed);

  for (const auto &binding : properties.Bindings()) {
    if (!binding) {
      continue;
    }
    const auto view = binding->View();
    Synchronize(state, view, now, reducedMotion, motionRestored,
                explicitMotionStarted, result.changed);
  }

  std::erase_if(state.tracks,
                [](const Track &track) { return !track.seen; });

  state.completionPending =
      state.completionPending || explicitMotionStarted;
  for (const auto &track : state.tracks) {
    if (!track.active) {
      continue;
    }
    result.activeElementCount = 1;
    const auto deadline = track.NextDeadline(now);
    if (deadline &&
        (!result.nextDeadline || *deadline < *result.nextDeadline)) {
      result.nextDeadline = deadline;
    }
  }
  if (result.activeElementCount == 0 && state.completionPending) {
    state.completionPending = false;
    if (properties.onSettled) {
      try {
        properties.onSettled();
      } catch (...) {
      }
    }
  }
  return result;
}

void AdvanceSubtree(RuntimeTree &tree, const ElementHandle handle,
                    const MonotonicTime now, const bool reducedMotion,
                    MotionFrameResult &result) {
  auto *node = tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  const auto nodeResult = AdvanceNode(*node, now, reducedMotion);
  result.changed = result.changed || nodeResult.changed;
  result.activeElementCount += nodeResult.activeElementCount;
  if (nodeResult.nextDeadline &&
      (!result.nextDeadline ||
       *nodeResult.nextDeadline < *result.nextDeadline)) {
    result.nextDeadline = nodeResult.nextDeadline;
  }
  const auto children = node->children;
  for (const auto child : children) {
    AdvanceSubtree(tree, child, now, reducedMotion, result);
  }
}

void CollectSubtreeDiagnostics(const RuntimeTree &tree,
                               const ElementHandle handle,
                               MotionDiagnostics &diagnostics) {
  const auto *node = tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  if (node->motion) {
    diagnostics.propertyConflictCount +=
        node->motion->propertyConflictCount;
    for (const auto &track : node->motion->tracks) {
      diagnostics.tracks.push_back(MotionTrackDiagnostics{
          .owner = node->id,
          .property = track.property,
          .propertyName = track.propertyName,
          .valueType = track.operations != nullptr
                           ? std::string_view{track.operations->typeName}
                           : std::string_view{},
          .interpolator = track.operations != nullptr
                              ? std::string_view{track.operations->typeName}
                              : std::string_view{},
          .timing = TimingName(track.spec.timing),
          .curve = CurveName(track.spec.timing),
          .customCurve = CustomCurve(track.spec.timing),
          .active = track.active,
          .evaluationFailureCount = track.evaluationFailureCount,
      });
    }
  }
  for (const auto child : node->children) {
    CollectSubtreeDiagnostics(tree, child, diagnostics);
  }
}

template <AnimatableValue T>
[[nodiscard]] auto Read(const MotionState *state,
                        const AnimationProperty<T> &property) noexcept -> T {
  auto result = property.DefaultValue();
  static_cast<void>(CopyMotionValue(state, property.Id(), typeid(T), &result));
  return result;
}
} // namespace

auto AdvanceMotion(RuntimeTree &tree, const MonotonicTime now,
                   const bool reducedMotion) -> MotionFrameResult {
  MotionFrameResult result{};
  AdvanceSubtree(tree, tree.Root(), now, reducedMotion, result);
  return result;
}

void CollectMotionDiagnostics(const RuntimeTree &tree,
                              MotionDiagnostics &diagnostics) {
  diagnostics.tracks.clear();
  diagnostics.propertyConflictCount = 0;
  CollectSubtreeDiagnostics(tree, tree.Root(), diagnostics);
}

auto CopyMotionValue(const MotionState *state,
                     const AnimationPropertyId property,
                     const std::type_info &type, void *output) noexcept -> bool {
  if (state == nullptr || output == nullptr) {
    return false;
  }
  const auto *track = FindTrack(*state, property);
  if (track == nullptr || track->operations == nullptr ||
      *track->operations->type != type) {
    return false;
  }
  return track->operations->assignCopy(output, track->value.Get());
}

auto IsMotionPropertyActive(const MotionState *state,
                            const AnimationPropertyId property) noexcept
    -> bool {
  const auto *track = state != nullptr ? FindTrack(*state, property) : nullptr;
  return track != nullptr && track->active;
}

auto IsAnyMotionPropertyActive(const MotionState *state) noexcept -> bool {
  return state != nullptr &&
         std::any_of(state->tracks.begin(), state->tracks.end(),
                     [](const Track &track) { return track.active; });
}

auto SnapshotFor(const RuntimeNode &node) noexcept -> MotionSnapshot {
  const auto *state = node.motion.get();
  return MotionSnapshot{
      .value = Read(state, MotionProperty::Value),
      .opacity = Read(state, MotionProperty::Opacity),
      .transform =
          MotionTransform{
              .translation = Read(state, MotionProperty::Translation),
              .scale = Read(state, MotionProperty::Scale),
          },
      .background = Read(state, MotionProperty::Background),
      .foreground = Read(state, MotionProperty::Foreground),
      .borderColor = Read(state, MotionProperty::BorderColor),
      .focusOpacity = Read(state, MotionProperty::FocusOpacity),
      .hasBackground = state != nullptr && state->hasBackground,
      .hasForeground = state != nullptr && state->hasForeground,
      .hasBorderColor = state != nullptr && state->hasBorderColor,
      .active = state != nullptr &&
                std::any_of(state->tracks.begin(), state->tracks.end(),
                            [](const Track &track) { return track.active; }),
  };
}

auto TransformFor(const RuntimeNode &node) noexcept -> MotionTransform {
  auto transform = SnapshotFor(node).transform;
  const auto centerX = node.arrangedBounds.x + node.arrangedBounds.width * 0.5F;
  const auto centerY = node.arrangedBounds.y + node.arrangedBounds.height * 0.5F;
  transform.translation.x += centerX * (1.0F - transform.scale.x);
  transform.translation.y += centerY * (1.0F - transform.scale.y);
  return transform;
}

auto TransformPoint(const Point point, const MotionTransform transform) noexcept
    -> Point {
  return Point{point.x * transform.scale.x + transform.translation.x,
               point.y * transform.scale.y + transform.translation.y};
}

auto InverseTransformPoint(const Point point,
                           const MotionTransform transform) noexcept -> Point {
  const auto scaleX = std::abs(transform.scale.x) > 0.0001F
                          ? transform.scale.x
                          : (transform.scale.x < 0.0F ? -0.0001F : 0.0001F);
  const auto scaleY = std::abs(transform.scale.y) > 0.0001F
                          ? transform.scale.y
                          : (transform.scale.y < 0.0F ? -0.0001F : 0.0001F);
  return Point{(point.x - transform.translation.x) / scaleX,
               (point.y - transform.translation.y) / scaleY};
}

auto TransformRect(const Rect rect, const MotionTransform transform) noexcept
    -> Rect {
  const auto first = TransformPoint(Point{rect.x, rect.y}, transform);
  const auto second = TransformPoint(
      Point{rect.x + rect.width, rect.y + rect.height}, transform);
  return Rect{std::min(first.x, second.x), std::min(first.y, second.y),
              std::abs(second.x - first.x),
              std::abs(second.y - first.y)};
}

auto ComposedTransformFor(const RuntimeTree &tree,
                          ElementHandle handle) noexcept -> MotionTransform {
  std::vector<ElementHandle> chain;
  while (const auto *node = tree.Get(handle)) {
    chain.push_back(handle);
    if (node->type == ElementType::Popup || !node->parent) {
      break;
    }
    handle = node->parent;
  }
  MotionTransform result{};
  for (auto current = chain.rbegin(); current != chain.rend(); ++current) {
    const auto *node = tree.Get(*current);
    if (node == nullptr) {
      continue;
    }
    const auto local = TransformFor(*node);
    result.translation = Point{
        result.translation.x + local.translation.x * result.scale.x,
        result.translation.y + local.translation.y * result.scale.y,
    };
    result.scale = Point{result.scale.x * local.scale.x,
                         result.scale.y * local.scale.y};
  }
  return result;
}

auto TransformedBoundsFor(const RuntimeTree &tree, const ElementHandle handle,
                          const Rect bounds) noexcept -> Rect {
  return TransformRect(bounds, ComposedTransformFor(tree, handle));
}
} // namespace NGIN::UI::Detail
