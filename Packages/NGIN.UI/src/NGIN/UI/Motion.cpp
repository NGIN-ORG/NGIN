#include <NGIN/UI/Animation.hpp>

#include "MotionInternal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

auto ApplyEasing(const Easing easing, const F32 progress) noexcept -> F32 {
  const auto value = std::clamp(progress, 0.0F, 1.0F);
  switch (easing) {
  case Easing::Linear:
    return value;
  case Easing::EaseIn:
    return value * value * value;
  case Easing::EaseOut: {
    const auto inverse = 1.0F - value;
    return 1.0F - inverse * inverse * inverse;
  }
  case Easing::EaseInOut:
    return value < 0.5F ? 4.0F * value * value * value
                        : 1.0F - std::pow(-2.0F * value + 2.0F, 3.0F) * 0.5F;
  case Easing::Standard:
  default:
    return value * value * (3.0F - 2.0F * value);
  }
}

auto Interpolate(const F32 start, const F32 end,
                 const F32 progress) noexcept -> F32 {
  return start + (end - start) * std::clamp(progress, 0.0F, 1.0F);
}

auto Interpolate(const Point start, const Point end,
                 const F32 progress) noexcept -> Point {
  return Point{Interpolate(start.x, end.x, progress),
               Interpolate(start.y, end.y, progress)};
}

auto Interpolate(const Color start, const Color end,
                 const F32 progress) noexcept -> Color {
  return Color{
      Interpolate(start.red, end.red, progress),
      Interpolate(start.green, end.green, progress),
      Interpolate(start.blue, end.blue, progress),
      Interpolate(start.alpha, end.alpha, progress),
  };
}
} // namespace NGIN::UI

namespace NGIN::UI::Detail {
constexpr UInt32 MaximumFiniteRepeatCount = 10'000;
constexpr auto MaximumDuration = std::chrono::minutes{1};

template <typename T> struct Track final {
  T value{};
  T start{};
  T target{};
  AnimationSpec spec{};
  MonotonicTime started{};
  const void *handleIdentity{nullptr};
  bool initialized{false};
  bool active{false};
};

struct MotionState final {
  Track<F32> value{};
  Track<F32> opacity{};
  Track<Point> translation{};
  Track<Point> scale{};
  Track<Color> background{};
  Track<Color> foreground{};
  Track<Color> borderColor{};
  Track<F32> focusOpacity{};
  bool hasBackground{false};
  bool hasForeground{false};
  bool hasBorderColor{false};
  bool completionPending{false};
  bool reducedMotion{false};
  bool reducedMotionInitialized{false};
};

[[nodiscard]] auto Normalize(AnimationSpec spec) noexcept -> AnimationSpec {
  spec.duration = std::clamp(spec.duration, std::chrono::milliseconds{0},
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 MaximumDuration));
  spec.delay = std::clamp(spec.delay, std::chrono::milliseconds{0},
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              MaximumDuration));
  if (spec.repeatCount != 0) {
    spec.repeatCount = std::clamp(spec.repeatCount, UInt32{1},
                                  MaximumFiniteRepeatCount);
  }
  return spec;
}

template <typename T>
[[nodiscard]] auto ValueAt(const Track<T> &track, const MonotonicTime now,
                           bool &finished) noexcept -> T {
  finished = false;
  const auto delay =
      std::chrono::duration_cast<MonotonicTime>(track.spec.delay);
  if (now <= track.started + delay) {
    return track.start;
  }
  const auto duration =
      std::chrono::duration_cast<MonotonicTime>(track.spec.duration);
  if (duration.count() <= 0) {
    finished = true;
    return track.target;
  }

  const auto elapsed = now - track.started - delay;
  const auto leg = static_cast<UInt64>(elapsed / duration);
  const auto finite = track.spec.repeatCount != 0;
  UInt64 legCount = std::numeric_limits<UInt64>::max();
  if (finite) {
    legCount = track.spec.repeatMode == AnimationRepeatMode::Reverse
                   ? static_cast<UInt64>(track.spec.repeatCount) * 2U - 1U
                   : static_cast<UInt64>(track.spec.repeatCount);
    if (leg >= legCount) {
      finished = true;
      return track.target;
    }
  }

  const auto remainder = elapsed % duration;
  const auto linear = static_cast<F32>(
      static_cast<F64>(remainder.count()) / static_cast<F64>(duration.count()));
  auto eased = ApplyEasing(track.spec.easing, linear);
  if (track.spec.repeatMode == AnimationRepeatMode::Reverse &&
      (leg % 2U) != 0U) {
    eased = 1.0F - eased;
  }
  return Interpolate(track.start, track.target, eased);
}

template <typename T>
auto Advance(Track<T> &track, const MonotonicTime now) noexcept -> bool {
  if (!track.active) {
    return false;
  }
  bool finished = false;
  const auto next = ValueAt(track, now, finished);
  const auto changed = next != track.value;
  track.value = next;
  track.active = !finished;
  return changed;
}

template <typename T>
auto Settle(Track<T> &track) noexcept -> bool {
  if (!track.initialized) {
    return false;
  }
  const auto changed = track.value != track.target;
  track.value = track.target;
  track.start = track.target;
  track.active = false;
  return changed;
}

template <typename T>
[[nodiscard]] auto NextDeadline(const Track<T> &track,
                                const MonotonicTime now) noexcept
    -> std::optional<MonotonicTime> {
  if (!track.active) {
    return std::nullopt;
  }
  const auto delayEnd =
      track.started + std::chrono::duration_cast<MonotonicTime>(track.spec.delay);
  if (now < delayEnd) {
    return delayEnd;
  }
  auto deadline = now + std::chrono::milliseconds{16};
  if (track.spec.repeatCount != 0) {
    const auto legs = track.spec.repeatMode == AnimationRepeatMode::Reverse
                          ? static_cast<UInt64>(track.spec.repeatCount) * 2U - 1U
                          : static_cast<UInt64>(track.spec.repeatCount);
    const auto end = delayEnd +
                     std::chrono::duration_cast<MonotonicTime>(
                         track.spec.duration) *
                         static_cast<MonotonicTime::rep>(legs);
    deadline = std::min(deadline, end);
  }
  return deadline;
}

template <typename T>
auto Synchronize(Track<T> &track, const T &target,
                 const std::optional<T> &initial, AnimationSpec spec,
                 const void *handleIdentity, const bool cancelled,
                 const MonotonicTime now, const bool reducedMotion,
                 bool &startedExplicitly) noexcept -> bool {
  spec = Normalize(spec);
  const auto first = !track.initialized;
  const auto declarationChanged =
      first || target != track.target || spec != track.spec ||
      handleIdentity != track.handleIdentity;
  if (!declarationChanged) {
    if (cancelled) {
      track.active = false;
    }
    return false;
  }

  track.initialized = true;
  track.start = first ? (initial ? *initial : target) : track.value;
  track.target = target;
  track.spec = spec;
  track.started = now;
  track.handleIdentity = handleIdentity;
  if (cancelled) {
    track.value = track.start;
    track.active = false;
    return false;
  }

  const auto canAnimate = !reducedMotion && track.start != track.target &&
                          track.spec.duration.count() > 0;
  track.value = canAnimate ? track.start : track.target;
  track.active = canAnimate;
  startedExplicitly = startedExplicitly || canAnimate || track.start != target;
  return true;
}

template <typename T>
auto Synchronize(Track<T> &track, const AnimationTarget<T> &target,
                 const MonotonicTime now, const bool reducedMotion,
                 bool &startedExplicitly) noexcept -> bool {
  return Synchronize(track, target.Target(), target.Initial(), target.Spec(),
                     target.HandleIdentity(), target.IsCancelled(), now,
                     reducedMotion, startedExplicitly);
}

template <typename T>
auto SynchronizeAutomatic(Track<T> &track, const T &target,
                          const AnimationSpec spec, const MonotonicTime now,
                          const bool reducedMotion) noexcept -> bool {
  bool ignored = false;
  return Synchronize(track, target, std::optional<T>{}, spec, nullptr, false,
                     now, reducedMotion, ignored);
}

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

auto AdvanceNode(RuntimeNode &node, const MonotonicTime now,
                 const bool reducedMotion) -> MotionFrameResult {
  if (!node.motion) {
    node.motion = std::make_shared<MotionState>();
  }
  auto &state = *node.motion;
  MotionFrameResult result{};
  const auto motionRestored = state.reducedMotionInitialized &&
                              state.reducedMotion && !reducedMotion;
  state.reducedMotion = reducedMotion;
  state.reducedMotionInitialized = true;
  if (motionRestored) {
    const auto restartRepeating = [](auto &track) {
      if (track.spec.repeatCount == 0) {
        track.initialized = false;
      }
    };
    restartRepeating(state.value);
    restartRepeating(state.opacity);
    restartRepeating(state.translation);
    restartRepeating(state.scale);
    restartRepeating(state.background);
    restartRepeating(state.foreground);
    restartRepeating(state.borderColor);
  }
  auto advance = [&](auto &track) { result.changed |= Advance(track, now); };
  advance(state.value);
  advance(state.opacity);
  advance(state.translation);
  advance(state.scale);
  advance(state.background);
  advance(state.foreground);
  advance(state.borderColor);
  advance(state.focusOpacity);

  if (reducedMotion) {
    auto settle = [&](auto &track) { result.changed |= Settle(track); };
    settle(state.value);
    settle(state.opacity);
    settle(state.translation);
    settle(state.scale);
    settle(state.background);
    settle(state.foreground);
    settle(state.borderColor);
    settle(state.focusOpacity);
  }

  bool explicitMotionStarted = false;
  const auto &properties = node.properties.motion;
  if (properties.value) {
    result.changed |= Synchronize(state.value, *properties.value, now,
                                  reducedMotion, explicitMotionStarted);
  } else {
    result.changed |= SynchronizeAutomatic(
        state.value, 0.0F, AnimationSpec{.duration = std::chrono::milliseconds{0}},
        now, reducedMotion);
  }
  if (properties.opacity) {
    result.changed |= Synchronize(state.opacity, *properties.opacity, now,
                                  reducedMotion, explicitMotionStarted);
  } else {
    result.changed |= SynchronizeAutomatic(
        state.opacity, 1.0F,
        AnimationSpec{.duration = std::chrono::milliseconds{0}}, now,
        reducedMotion);
  }
  if (properties.translation) {
    result.changed |= Synchronize(state.translation, *properties.translation,
                                  now, reducedMotion, explicitMotionStarted);
  } else {
    result.changed |= SynchronizeAutomatic(
        state.translation, Point{},
        AnimationSpec{.duration = std::chrono::milliseconds{0}}, now,
        reducedMotion);
  }
  if (properties.scale) {
    result.changed |= Synchronize(state.scale, *properties.scale, now,
                                  reducedMotion, explicitMotionStarted);
  } else {
    result.changed |= SynchronizeAutomatic(
        state.scale, Point{1.0F, 1.0F},
        AnimationSpec{.duration = std::chrono::milliseconds{0}}, now,
        reducedMotion);
  }

  auto style = ResolveVisualStyle(node.properties.visual, VisualStateFor(node));
  if (!style.background && node.properties.paintsBackground) {
    style.background = node.properties.background;
  }
  const auto transition = node.properties.visual.transition;
  if (properties.background) {
    state.hasBackground = true;
    result.changed |= Synchronize(state.background, *properties.background,
                                  now, reducedMotion, explicitMotionStarted);
  } else if (style.background) {
    state.hasBackground = true;
    result.changed |= SynchronizeAutomatic(state.background, *style.background,
                                            transition, now, reducedMotion);
  } else {
    state.hasBackground = false;
  }
  if (properties.foreground) {
    state.hasForeground = true;
    result.changed |= Synchronize(state.foreground, *properties.foreground,
                                  now, reducedMotion, explicitMotionStarted);
  } else if (style.foreground) {
    state.hasForeground = true;
    result.changed |= SynchronizeAutomatic(state.foreground, *style.foreground,
                                            transition, now, reducedMotion);
  } else {
    state.hasForeground = false;
  }
  if (properties.borderColor) {
    state.hasBorderColor = true;
    result.changed |= Synchronize(state.borderColor, *properties.borderColor,
                                  now, reducedMotion, explicitMotionStarted);
  } else if (style.borderColor) {
    state.hasBorderColor = true;
    result.changed |= SynchronizeAutomatic(state.borderColor,
                                            *style.borderColor, transition, now,
                                            reducedMotion);
  } else {
    state.hasBorderColor = false;
  }
  result.changed |= SynchronizeAutomatic(
      state.focusOpacity,
      HasVisualState(VisualStateFor(node), VisualStateFlags::Focused) ? 1.0F
                                                                      : 0.0F,
      transition, now, reducedMotion);

  state.completionPending = state.completionPending || explicitMotionStarted;
  const auto active = state.value.active || state.opacity.active ||
                      state.translation.active || state.scale.active ||
                      state.background.active || state.foreground.active ||
                      state.borderColor.active || state.focusOpacity.active;
  if (active) {
    result.activeElementCount = 1;
    const auto mergeDeadline = [&](const auto &track) {
      const auto deadline = NextDeadline(track, now);
      if (deadline &&
          (!result.nextDeadline || *deadline < *result.nextDeadline)) {
        result.nextDeadline = deadline;
      }
    };
    mergeDeadline(state.value);
    mergeDeadline(state.opacity);
    mergeDeadline(state.translation);
    mergeDeadline(state.scale);
    mergeDeadline(state.background);
    mergeDeadline(state.foreground);
    mergeDeadline(state.borderColor);
    mergeDeadline(state.focusOpacity);
  } else if (state.completionPending) {
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
auto AdvanceMotion(RuntimeTree &tree, const MonotonicTime now,
                   const bool reducedMotion) -> MotionFrameResult {
  MotionFrameResult result{};
  AdvanceSubtree(tree, tree.Root(), now, reducedMotion, result);
  return result;
}

auto SnapshotFor(const RuntimeNode &node) noexcept -> MotionSnapshot {
  if (!node.motion) {
    return {};
  }
  const auto &state = *node.motion;
  return MotionSnapshot{
      .value = state.value.value,
      .opacity = std::clamp(state.opacity.value, 0.0F, 1.0F),
      .transform =
          MotionTransform{
              .translation = state.translation.value,
              .scale = state.scale.value,
          },
      .background = state.background.value,
      .foreground = state.foreground.value,
      .borderColor = state.borderColor.value,
      .focusOpacity = std::clamp(state.focusOpacity.value, 0.0F, 1.0F),
      .hasBackground = state.hasBackground,
      .hasForeground = state.hasForeground,
      .hasBorderColor = state.hasBorderColor,
      .active = state.value.active || state.opacity.active ||
                state.translation.active || state.scale.active ||
                state.background.active || state.foreground.active ||
                state.borderColor.active || state.focusOpacity.active,
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
