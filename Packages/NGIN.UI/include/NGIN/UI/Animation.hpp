#pragma once

#include <NGIN/Primitives.hpp>

#include <atomic>
#include <chrono>
#include <compare>
#include <memory>
#include <optional>
#include <utility>

namespace NGIN::UI {
/// @brief Monotonic duration used by platform clocks and animation deadlines.
using MonotonicTime = std::chrono::nanoseconds;

/// @brief Standard curves available to target-value animations.
enum class Easing : UInt8 {
  Linear,
  Standard,
  EaseIn,
  EaseOut,
  EaseInOut,
};

/// @brief Behavior used between repeated animation passes.
enum class AnimationRepeatMode : UInt8 {
  Restart,
  Reverse,
};

/// @brief Duration, curve, delay, and bounded repetition for an animation.
struct AnimationSpec final {
  std::chrono::milliseconds duration{180};
  std::chrono::milliseconds delay{0};
  Easing easing{Easing::Standard};
  UInt32 repeatCount{1};
  AnimationRepeatMode repeatMode{AnimationRepeatMode::Restart};

  [[nodiscard]] constexpr auto
  operator<=>(const AnimationSpec &) const noexcept = default;
};

namespace Detail {
/// @brief Shared cancellation flag carried by animation declarations.
struct AnimationCancellation final {
  std::atomic_bool cancelled{false};
};
} // namespace Detail

template <typename T> class AnimationTarget;

/// @brief Move-safe token that can explicitly stop an authored animation.
class AnimationHandle final {
public:
  AnimationHandle();
  AnimationHandle(const AnimationHandle &) = delete;
  AnimationHandle(AnimationHandle &&) noexcept = default;
  auto operator=(const AnimationHandle &) -> AnimationHandle & = delete;
  auto operator=(AnimationHandle &&) noexcept -> AnimationHandle & = default;
  ~AnimationHandle() = default;

  void Cancel() noexcept;
  [[nodiscard]] auto IsCancelled() const noexcept -> bool;

private:
  template <typename T>
  friend auto Animate(T target, AnimationSpec spec,
                      const AnimationHandle &handle) -> AnimationTarget<T>;
  template <typename T>
  friend auto AnimateFrom(T initial, T target, AnimationSpec spec,
                          const AnimationHandle &handle) -> AnimationTarget<T>;

  std::shared_ptr<Detail::AnimationCancellation> m_cancellation;
};

/// @brief Target and timing declaration retained by a keyed UI element.
template <typename T> class AnimationTarget final {
public:
  AnimationTarget() = default;

  [[nodiscard]] auto Target() const noexcept -> const T & { return m_target; }
  [[nodiscard]] auto Initial() const noexcept -> const std::optional<T> & {
    return m_initial;
  }
  [[nodiscard]] auto Spec() const noexcept -> const AnimationSpec & {
    return m_spec;
  }
  [[nodiscard]] auto IsCancelled() const noexcept -> bool {
    const auto cancellation = m_cancellation.lock();
    return cancellation != nullptr &&
           cancellation->cancelled.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto HandleIdentity() const noexcept -> const void * {
    const auto cancellation = m_cancellation.lock();
    return cancellation.get();
  }

private:
  template <typename Value>
  friend auto Animate(Value target, AnimationSpec spec)
      -> AnimationTarget<Value>;
  template <typename Value>
  friend auto Animate(Value target, AnimationSpec spec,
                      const AnimationHandle &handle) -> AnimationTarget<Value>;
  template <typename Value>
  friend auto AnimateFrom(Value initial, Value target, AnimationSpec spec)
      -> AnimationTarget<Value>;
  template <typename Value>
  friend auto AnimateFrom(Value initial, Value target, AnimationSpec spec,
                          const AnimationHandle &handle)
      -> AnimationTarget<Value>;

  T m_target{};
  std::optional<T> m_initial{};
  AnimationSpec m_spec{};
  std::weak_ptr<Detail::AnimationCancellation> m_cancellation{};
};

/// @brief Declares a new target while retained state supplies the start value.
template <typename T>
[[nodiscard]] auto Animate(T target, AnimationSpec spec = {})
    -> AnimationTarget<T> {
  AnimationTarget<T> result{};
  result.m_target = std::move(target);
  result.m_spec = spec;
  return result;
}

/// @brief Declares a cancellable target while retained state supplies the start.
template <typename T>
[[nodiscard]] auto Animate(T target, AnimationSpec spec,
                           const AnimationHandle &handle)
    -> AnimationTarget<T> {
  auto result = Animate(std::move(target), spec);
  result.m_cancellation = handle.m_cancellation;
  return result;
}

/// @brief Declares an initial value used only when the keyed element first mounts.
template <typename T>
[[nodiscard]] auto AnimateFrom(T initial, T target, AnimationSpec spec = {})
    -> AnimationTarget<T> {
  auto result = Animate(std::move(target), spec);
  result.m_initial = std::move(initial);
  return result;
}

/// @brief Declares a cancellable target with a first-mount initial value.
template <typename T>
[[nodiscard]] auto AnimateFrom(T initial, T target, AnimationSpec spec,
                               const AnimationHandle &handle)
    -> AnimationTarget<T> {
  auto result = AnimateFrom(std::move(initial), std::move(target), spec);
  result.m_cancellation = handle.m_cancellation;
  return result;
}

/// @brief Evaluates a standard easing curve for a normalized progress value.
[[nodiscard]] auto ApplyEasing(Easing easing, F32 progress) noexcept -> F32;
} // namespace NGIN::UI
