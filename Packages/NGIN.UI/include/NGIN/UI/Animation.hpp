#pragma once

#include <NGIN/Primitives.hpp>

#include <atomic>
#include <chrono>
#include <concepts>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace NGIN::UI {
namespace Detail {
/// @brief Internal evaluator with access to compact curve storage.
struct EasingCurveAccess;
}
/// @brief Monotonic duration used by platform clocks and animation deadlines.
using MonotonicTime = std::chrono::nanoseconds;

/// @brief Allocation-free curves supplied by NGIN.UI.
enum class BuiltInEasingCurve : UInt8 {
  Linear,
  Standard,
  EaseIn,
  EaseOut,
  EaseInOut,
  CubicBezier,
  Steps,
  Custom,
};

/// @brief Determines whether a stepped curve advances at interval starts or ends.
enum class StepPosition : UInt8 {
  Start,
  End,
};

/// @brief Immutable application extension point for a normalized easing curve.
///
/// Evaluate receives an input in [0, 1]. Implementations may return values
/// outside that range to create deliberate overshoot. Implementations must be
/// safe for concurrent reads. Exceptions and non-finite results are contained
/// by EasingCurve and fall back to linear progress.
class IEasingCurve {
public:
  virtual ~IEasingCurve() = default;

  [[nodiscard]] virtual auto Evaluate(F32 progress) const -> F32 = 0;
  [[nodiscard]] virtual auto Name() const noexcept -> std::string_view {
    return "Custom";
  }
};

/// @brief Copyable built-in or application-defined easing curve.
class EasingCurve final {
public:
  EasingCurve() noexcept = default;

  [[nodiscard]] static auto Linear() noexcept -> EasingCurve;
  [[nodiscard]] static auto Standard() noexcept -> EasingCurve;
  [[nodiscard]] static auto EaseIn() noexcept -> EasingCurve;
  [[nodiscard]] static auto EaseOut() noexcept -> EasingCurve;
  [[nodiscard]] static auto EaseInOut() noexcept -> EasingCurve;
  [[nodiscard]] static auto CubicBezier(F32 x1, F32 y1, F32 x2,
                                        F32 y2) noexcept -> EasingCurve;
  [[nodiscard]] static auto Steps(UInt32 count,
                                  StepPosition position = StepPosition::End)
      noexcept -> EasingCurve;
  [[nodiscard]] static auto Custom(std::shared_ptr<const IEasingCurve> curve)
      noexcept -> EasingCurve;

  template <typename T, typename... Args>
    requires std::derived_from<T, IEasingCurve>
  [[nodiscard]] static auto MakeCustom(Args &&...args) -> EasingCurve {
    return Custom(std::make_shared<const T>(std::forward<Args>(args)...));
  }

  [[nodiscard]] auto Evaluate(F32 progress) const noexcept -> F32;
  [[nodiscard]] auto Kind() const noexcept -> BuiltInEasingCurve;
  [[nodiscard]] auto Name() const noexcept -> std::string_view;
  [[nodiscard]] auto IsCustom() const noexcept -> bool;

  friend auto operator==(const EasingCurve &left,
                         const EasingCurve &right) noexcept -> bool;

private:
  friend struct Detail::EasingCurveAccess;

  BuiltInEasingCurve m_kind{BuiltInEasingCurve::Standard};
  F32 m_x1{0.0F};
  F32 m_y1{0.0F};
  F32 m_x2{1.0F};
  F32 m_y2{1.0F};
  UInt32 m_stepCount{1};
  StepPosition m_stepPosition{StepPosition::End};
  std::shared_ptr<const IEasingCurve> m_custom{};
};

namespace Detail {
/// @brief Evaluates a curve while reporting contained custom-code failures.
[[nodiscard]] auto EvaluateEasingCurve(const EasingCurve &curve, F32 progress,
                                       bool &valid) noexcept -> F32;
}

/// @brief Fixed-duration timing using an easing curve.
struct TweenTiming final {
  std::chrono::milliseconds duration{180};
  EasingCurve curve{EasingCurve::Standard()};

  friend auto operator==(const TweenTiming &,
                         const TweenTiming &) noexcept
      -> bool = default;
};

/// @brief Physical spring timing that settles according to displacement and velocity.
struct SpringTiming final {
  F32 mass{1.0F};
  F32 stiffness{220.0F};
  F32 damping{20.0F};
  F32 initialVelocity{0.0F};
  F32 restDisplacement{0.001F};
  F32 restVelocity{0.001F};
  std::chrono::milliseconds maximumDuration{5000};

  friend auto operator==(const SpringTiming &,
                         const SpringTiming &) noexcept
      -> bool = default;
};

/// @brief Timing model used by a target-value animation.
using AnimationTiming = std::variant<TweenTiming, SpringTiming>;

/// @brief Behavior used between repeated animation passes.
enum class AnimationRepeatMode : UInt8 {
  Restart,
  Reverse,
};

/// @brief Timing, delay, and bounded repetition for an animation.
struct AnimationSpec final {
  AnimationTiming timing{TweenTiming{}};
  std::chrono::milliseconds delay{0};
  UInt32 repeatCount{1};
  AnimationRepeatMode repeatMode{AnimationRepeatMode::Restart};

  friend auto operator==(const AnimationSpec &,
                         const AnimationSpec &) noexcept
      -> bool = default;
};

/// @brief Customization point that interpolates an application value type.
template <typename T> struct AnimationInterpolator;

/// @brief Scalar animation interpolation supplied by NGIN.UI.
template <> struct AnimationInterpolator<F32> final {
  [[nodiscard]] static auto Interpolate(F32 start, F32 end,
                                        F32 progress) noexcept -> F32 {
    return start + (end - start) * progress;
  }
};

template <typename T>
concept AnimatableValue =
    std::default_initializable<T> && std::copy_constructible<T> &&
    std::is_copy_assignable_v<T> &&
    std::equality_comparable<T> &&
    requires(const T &start, const T &end, const F32 progress) {
      { AnimationInterpolator<T>::Interpolate(start, end, progress) } ->
          std::convertible_to<T>;
    };

/// @brief Interpolates a supported built-in or application-defined value.
template <AnimatableValue T>
[[nodiscard]] auto InterpolateAnimationValue(const T &start, const T &end,
                                             const F32 progress) -> T {
  return AnimationInterpolator<T>::Interpolate(start, end, progress);
}

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
  result.m_spec = std::move(spec);
  return result;
}

/// @brief Declares a cancellable target while retained state supplies the start.
template <typename T>
[[nodiscard]] auto Animate(T target, AnimationSpec spec,
                           const AnimationHandle &handle)
    -> AnimationTarget<T> {
  auto result = Animate(std::move(target), std::move(spec));
  result.m_cancellation = handle.m_cancellation;
  return result;
}

/// @brief Declares an initial value used only when the keyed element first mounts.
template <typename T>
[[nodiscard]] auto AnimateFrom(T initial, T target, AnimationSpec spec = {})
    -> AnimationTarget<T> {
  auto result = Animate(std::move(target), std::move(spec));
  result.m_initial = std::move(initial);
  return result;
}

/// @brief Declares a cancellable target with a first-mount initial value.
template <typename T>
[[nodiscard]] auto AnimateFrom(T initial, T target, AnimationSpec spec,
                               const AnimationHandle &handle)
    -> AnimationTarget<T> {
  auto result =
      AnimateFrom(std::move(initial), std::move(target), std::move(spec));
  result.m_cancellation = handle.m_cancellation;
  return result;
}

/// @brief Evaluates an easing curve for normalized progress.
[[nodiscard]] auto ApplyEasing(const EasingCurve &curve, F32 progress) noexcept
    -> F32;
} // namespace NGIN::UI
