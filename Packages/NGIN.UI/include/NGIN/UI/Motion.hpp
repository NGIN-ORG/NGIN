#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/UI/Animation.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/Style.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

namespace NGIN::UI {
/// @brief Final result reported by an awaited motion operation.
enum class MotionOutcome : UInt8 {
  Completed,
  Canceled,
  Interrupted,
  Unmounted,
};

/// @brief Translation and scale applied after layout without changing layout
/// size.
struct MotionTransform final {
  Point translation{};
  Point scale{1.0F, 1.0F};

  [[nodiscard]] constexpr auto
  operator<=>(const MotionTransform &) const noexcept = default;
};

/// @brief Stable identifier for one typed animation property.
struct AnimationPropertyId final {
  UInt64 value{0};

  [[nodiscard]] constexpr auto
  operator<=>(const AnimationPropertyId &) const noexcept = default;
};

/// @brief Constraint applied after interpolation for a property value.
enum class AnimationOutputPolicy : UInt8 {
  Unbounded,
  UnitInterval,
  ColorChannels,
};

/// @brief Customization point for property-specific output constraints.
template <typename T> struct AnimationValuePolicy {
  static void Apply(T &, AnimationOutputPolicy) noexcept {}
};

/// @brief Unit-interval output constraints for scalar properties.
template <> struct AnimationValuePolicy<F32> final {
  static void Apply(F32 &value, const AnimationOutputPolicy policy) noexcept {
    if (policy == AnimationOutputPolicy::UnitInterval) {
      value = std::clamp(value, 0.0F, 1.0F);
    }
  }
};

/// @brief Point interpolation supplied by NGIN.UI.
template <> struct AnimationInterpolator<Point> final {
  [[nodiscard]] static auto Interpolate(const Point start, const Point end,
                                        const F32 progress) noexcept -> Point {
    return Point{
        AnimationInterpolator<F32>::Interpolate(start.x, end.x, progress),
        AnimationInterpolator<F32>::Interpolate(start.y, end.y, progress),
    };
  }
};

/// @brief RGBA interpolation supplied by NGIN.UI.
template <> struct AnimationInterpolator<Color> final {
  [[nodiscard]] static auto Interpolate(const Color start, const Color end,
                                        const F32 progress) noexcept -> Color {
    return Color{
        AnimationInterpolator<F32>::Interpolate(start.red, end.red, progress),
        AnimationInterpolator<F32>::Interpolate(start.green, end.green,
                                                progress),
        AnimationInterpolator<F32>::Interpolate(start.blue, end.blue, progress),
        AnimationInterpolator<F32>::Interpolate(start.alpha, end.alpha,
                                                progress),
    };
  }
};

/// @brief Color-channel output constraints supplied by NGIN.UI.
template <> struct AnimationValuePolicy<Color> final {
  static void Apply(Color &value, const AnimationOutputPolicy policy) noexcept {
    if (policy != AnimationOutputPolicy::ColorChannels) {
      return;
    }
    value.red = std::clamp(value.red, 0.0F, 1.0F);
    value.green = std::clamp(value.green, 0.0F, 1.0F);
    value.blue = std::clamp(value.blue, 0.0F, 1.0F);
    value.alpha = std::clamp(value.alpha, 0.0F, 1.0F);
  }
};

namespace Detail {
/// @brief Internal retained controller state.
struct MotionControllerState;
/// @brief Internal completion state for one controller operation.
struct MotionOperationState;
/// @brief Internal access to controller data attached to motion properties.
struct MotionAccess;

/// @brief Internal precedence used while synchronizing motion declarations.
enum class AnimationDeclarationSource : UInt8 {
  Automatic,
  Controller,
  Declarative,
};

[[nodiscard]] constexpr auto
HashAnimationProperty(const std::string_view name) noexcept
    -> AnimationPropertyId {
  UInt64 hash = 14695981039346656037ULL;
  for (const auto character : name) {
    hash ^= static_cast<UInt8>(character);
    hash *= 1099511628211ULL;
  }
  return AnimationPropertyId{hash};
}

/// @brief Internal type-erased operations for one interpolated value type.
struct AnimationValueOperations final {
  const std::type_info *type{nullptr};
  const char *typeName{nullptr};
  UIntSize size{0};
  UIntSize alignment{0};
  bool (*constructCopy)(void *, const void *) noexcept {nullptr};
  void (*destroy)(void *) noexcept {nullptr};
  bool (*assignCopy)(void *, const void *) noexcept {nullptr};
  bool (*equals)(const void *, const void *) noexcept {nullptr};
  bool (*interpolate)(const void *, const void *, F32,
                      void *) noexcept {nullptr};
  void (*applyPolicy)(void *, AnimationOutputPolicy) noexcept {nullptr};
};

template <AnimatableValue T>
[[nodiscard]] auto AnimationValueOperationsFor() noexcept
    -> const AnimationValueOperations & {
  static const AnimationValueOperations operations{
      .type = &typeid(T),
      .typeName = typeid(T).name(),
      .size = sizeof(T),
      .alignment = alignof(T),
      .constructCopy =
          [](void *destination, const void *source) noexcept {
            try {
              ::new (destination) T(*static_cast<const T *>(source));
              return true;
            } catch (...) {
              return false;
            }
          },
      .destroy = [](void *value) noexcept { static_cast<T *>(value)->~T(); },
      .assignCopy =
          [](void *destination, const void *source) noexcept {
            try {
              *static_cast<T *>(destination) = *static_cast<const T *>(source);
              return true;
            } catch (...) {
              return false;
            }
          },
      .equals =
          [](const void *left, const void *right) noexcept {
            try {
              return *static_cast<const T *>(left) ==
                     *static_cast<const T *>(right);
            } catch (...) {
              return false;
            }
          },
      .interpolate =
          [](const void *start, const void *end, const F32 progress,
             void *output) noexcept {
            try {
              *static_cast<T *>(output) = AnimationInterpolator<T>::Interpolate(
                  *static_cast<const T *>(start), *static_cast<const T *>(end),
                  progress);
              return true;
            } catch (...) {
              return false;
            }
          },
      .applyPolicy =
          [](void *value, const AnimationOutputPolicy policy) noexcept {
            AnimationValuePolicy<T>::Apply(*static_cast<T *>(value), policy);
          },
  };
  return operations;
}

/// @brief Internal non-owning view of one typed property declaration.
struct AnimationDeclarationView final {
  AnimationPropertyId property{};
  std::string_view propertyName{};
  AnimationOutputPolicy outputPolicy{AnimationOutputPolicy::Unbounded};
  const AnimationValueOperations *operations{nullptr};
  const void *target{nullptr};
  const void *initial{nullptr};
  const AnimationSpec *spec{nullptr};
  const void *handleIdentity{nullptr};
  std::shared_ptr<MotionOperationState> operation{};
  AnimationDeclarationSource source{AnimationDeclarationSource::Declarative};
  bool cancelled{false};
  bool builtIn{false};
};

/// @brief Internal owner interface for a typed property declaration.
class IAnimationBinding {
public:
  virtual ~IAnimationBinding() = default;
  [[nodiscard]] virtual auto View() const noexcept
      -> AnimationDeclarationView = 0;
};

[[nodiscard]] auto
BeginControllerMotion(NGIN::Async::TaskContext &context,
                      const std::shared_ptr<MotionControllerState> &controller,
                      std::shared_ptr<const IAnimationBinding> binding)
    -> NGIN::Async::Task<MotionOutcome>;
void CancelControllerMotion(const std::shared_ptr<MotionControllerState> &state,
                            AnimationPropertyId property) noexcept;
void CancelAllControllerMotion(
    const std::shared_ptr<MotionControllerState> &state) noexcept;
} // namespace Detail

/// @brief Typed stable key and default value for a generic animation track.
///
/// Names must have static lifetime and should be globally qualified, for
/// example "Acme.Gauge.Sweep". Runtime type and name checks detect accidental
/// reuse of the same hashed identifier.
template <AnimatableValue T> class AnimationProperty final {
public:
  AnimationProperty(
      std::string_view name, T defaultValue,
      AnimationOutputPolicy outputPolicy = AnimationOutputPolicy::Unbounded)
      : m_id(Detail::HashAnimationProperty(name)), m_name(name),
        m_defaultValue(std::move(defaultValue)), m_outputPolicy(outputPolicy) {}

  [[nodiscard]] static auto
  BuiltIn(std::string_view name, T defaultValue,
          AnimationOutputPolicy outputPolicy = AnimationOutputPolicy::Unbounded)
      -> AnimationProperty {
    AnimationProperty result{name, std::move(defaultValue), outputPolicy};
    result.m_builtIn = true;
    return result;
  }

  [[nodiscard]] auto Id() const noexcept -> AnimationPropertyId { return m_id; }
  [[nodiscard]] auto Name() const noexcept -> std::string_view {
    return m_name;
  }
  [[nodiscard]] auto DefaultValue() const noexcept -> const T & {
    return m_defaultValue;
  }
  [[nodiscard]] auto OutputPolicy() const noexcept -> AnimationOutputPolicy {
    return m_outputPolicy;
  }
  [[nodiscard]] auto IsBuiltIn() const noexcept -> bool { return m_builtIn; }

private:
  AnimationPropertyId m_id{};
  std::string_view m_name{};
  T m_defaultValue;
  AnimationOutputPolicy m_outputPolicy{AnimationOutputPolicy::Unbounded};
  bool m_builtIn{false};
};

namespace Detail {
/// @brief Internal owner for one typed animation declaration.
template <AnimatableValue T>
class AnimationBinding final : public IAnimationBinding {
public:
  AnimationBinding(AnimationProperty<T> property, AnimationTarget<T> target)
      : m_property(std::move(property)), m_target(std::move(target)) {}

  [[nodiscard]] auto View() const noexcept
      -> AnimationDeclarationView override {
    return AnimationDeclarationView{
        .property = m_property.Id(),
        .propertyName = m_property.Name(),
        .outputPolicy = m_property.OutputPolicy(),
        .operations = &AnimationValueOperationsFor<T>(),
        .target = &m_target.Target(),
        .initial = m_target.Initial() ? &*m_target.Initial() : nullptr,
        .spec = &m_target.Spec(),
        .handleIdentity = m_target.HandleIdentity(),
        .cancelled = m_target.IsCancelled(),
        .builtIn = m_property.IsBuiltIn(),
    };
  }

private:
  AnimationProperty<T> m_property;
  AnimationTarget<T> m_target;
};

/// @brief Internal retained animation tracks for one runtime element.
struct MotionState;
[[nodiscard]] auto CopyMotionValue(const MotionState *state,
                                   AnimationPropertyId property,
                                   const std::type_info &type,
                                   void *output) noexcept -> bool;
[[nodiscard]] auto IsMotionPropertyActive(const MotionState *state,
                                          AnimationPropertyId property) noexcept
    -> bool;
[[nodiscard]] auto IsAnyMotionPropertyActive(const MotionState *state) noexcept
    -> bool;
} // namespace Detail

/// @brief Built-in typed properties consumed by standard NGIN.UI visuals.
namespace MotionProperty {
inline const AnimationProperty<F32> Value =
    AnimationProperty<F32>::BuiltIn("NGIN.UI.Value", 0.0F);
inline const AnimationProperty<F32> Opacity = AnimationProperty<F32>::BuiltIn(
    "NGIN.UI.Opacity", 1.0F, AnimationOutputPolicy::UnitInterval);
inline const AnimationProperty<Point> Translation =
    AnimationProperty<Point>::BuiltIn("NGIN.UI.Translation", Point{});
inline const AnimationProperty<Point> Scale =
    AnimationProperty<Point>::BuiltIn("NGIN.UI.Scale", Point{1.0F, 1.0F});
inline const AnimationProperty<Color> Background =
    AnimationProperty<Color>::BuiltIn("NGIN.UI.Background", Color{},
                                      AnimationOutputPolicy::ColorChannels);
inline const AnimationProperty<Color> Foreground =
    AnimationProperty<Color>::BuiltIn("NGIN.UI.Foreground", Color{},
                                      AnimationOutputPolicy::ColorChannels);
inline const AnimationProperty<Color> BorderColor =
    AnimationProperty<Color>::BuiltIn("NGIN.UI.BorderColor", Color{},
                                      AnimationOutputPolicy::ColorChannels);
inline const AnimationProperty<F32> FocusOpacity =
    AnimationProperty<F32>::BuiltIn("NGIN.UI.FocusOpacity", 0.0F,
                                    AnimationOutputPolicy::UnitInterval);
} // namespace MotionProperty

/// @brief Target-value motion attached to one declarative element.
struct MotionProperties final {
  std::optional<AnimationTarget<F32>> value{};
  std::optional<AnimationTarget<F32>> opacity{};
  std::optional<AnimationTarget<Point>> translation{};
  std::optional<AnimationTarget<Point>> scale{};
  std::optional<AnimationTarget<Color>> background{};
  std::optional<AnimationTarget<Color>> foreground{};
  std::optional<AnimationTarget<Color>> borderColor{};
  NGIN::Utilities::Callable<void()> onSettled{};

  template <AnimatableValue T>
  void Set(AnimationProperty<T> property, AnimationTarget<T> target) {
    auto replacement = std::make_shared<const Detail::AnimationBinding<T>>(
        std::move(property), std::move(target));
    const auto id = replacement->View().property;
    for (auto &binding : m_bindings) {
      if (binding->View().property == id) {
        binding = std::move(replacement);
        return;
      }
    }
    m_bindings.push_back(std::move(replacement));
  }

  void Remove(const AnimationPropertyId property) noexcept {
    std::erase_if(m_bindings, [property](const auto &binding) {
      return binding->View().property == property;
    });
  }

  [[nodiscard]] auto Bindings() const noexcept
      -> std::span<const std::shared_ptr<const Detail::IAnimationBinding>> {
    return m_bindings;
  }

private:
  friend class MotionController;
  friend struct Detail::MotionAccess;

  std::vector<std::shared_ptr<const Detail::IAnimationBinding>> m_bindings{};
  std::shared_ptr<Detail::MotionControllerState> m_controller{};
};

/// @brief Retained handle for starting and awaiting motion on a composed
/// element.
///
/// Attach the controller while composing the same keyed element on every pass.
/// A newer target for the same property interrupts the previous waiter.
class MotionController final {
public:
  MotionController();
  MotionController(const MotionController &) = delete;
  MotionController(MotionController &&) noexcept;
  auto operator=(const MotionController &) -> MotionController & = delete;
  auto operator=(MotionController &&) noexcept -> MotionController &;
  ~MotionController();

  /// @brief Binds this controller to the element owning these motion
  /// properties.
  void Attach(MotionProperties &properties) const noexcept;

  /// @brief Animates any registered property using the shared motion engine.
  template <AnimatableValue T>
  [[nodiscard]] auto AnimateToAsync(NGIN::Async::TaskContext &context,
                                    AnimationProperty<T> property, T target,
                                    AnimationSpec spec = {})
      -> NGIN::Async::Task<MotionOutcome> {
    auto initial = property.DefaultValue();
    auto binding = std::make_shared<const Detail::AnimationBinding<T>>(
        std::move(property),
        AnimateFrom(std::move(initial), std::move(target), std::move(spec)));
    return Detail::BeginControllerMotion(context, m_state, std::move(binding));
  }

  /// @brief Animates the element opacity.
  [[nodiscard]] auto FadeToAsync(NGIN::Async::TaskContext &context, F32 opacity,
                                 AnimationSpec spec = {})
      -> NGIN::Async::Task<MotionOutcome> {
    return AnimateToAsync(context, MotionProperty::Opacity, opacity,
                          std::move(spec));
  }

  /// @brief Animates the element translation.
  [[nodiscard]] auto TranslateToAsync(NGIN::Async::TaskContext &context,
                                      Point translation,
                                      AnimationSpec spec = {})
      -> NGIN::Async::Task<MotionOutcome> {
    return AnimateToAsync(context, MotionProperty::Translation, translation,
                          std::move(spec));
  }

  /// @brief Animates the element scale.
  [[nodiscard]] auto ScaleToAsync(NGIN::Async::TaskContext &context,
                                  Point scale, AnimationSpec spec = {})
      -> NGIN::Async::Task<MotionOutcome> {
    return AnimateToAsync(context, MotionProperty::Scale, scale,
                          std::move(spec));
  }

  /// @brief Animates a color property, defaulting to the background.
  [[nodiscard]] auto
  ColorToAsync(NGIN::Async::TaskContext &context, Color color,
               AnimationSpec spec = {},
               AnimationProperty<Color> property = MotionProperty::Background)
      -> NGIN::Async::Task<MotionOutcome> {
    return AnimateToAsync(context, std::move(property), color, std::move(spec));
  }

  /// @brief Cancels the current operation for one property.
  void Cancel(AnimationPropertyId property) noexcept;

  /// @brief Cancels every operation started by this controller.
  void CancelAll() noexcept;

private:
  std::shared_ptr<Detail::MotionControllerState> m_state{};
};

/// @brief Runtime description of one retained animation track.
struct MotionTrackDiagnostics final {
  ElementId owner{};
  AnimationPropertyId property{};
  std::string_view propertyName{};
  std::string_view valueType{};
  std::string_view interpolator{};
  std::string_view timing{};
  std::string_view curve{};
  bool customCurve{false};
  bool active{false};
  UInt64 evaluationFailureCount{0};
};

/// @brief Property identities, timing choices, and failures for a window.
struct MotionDiagnostics final {
  std::vector<MotionTrackDiagnostics> tracks{};
  UInt64 propertyConflictCount{0};
};

/// @brief Linearly interpolates two scalar values without clamping progress.
[[nodiscard]] inline auto Interpolate(const F32 start, const F32 end,
                                      const F32 progress) noexcept -> F32 {
  return AnimationInterpolator<F32>::Interpolate(start, end, progress);
}

/// @brief Linearly interpolates two points without clamping progress.
[[nodiscard]] inline auto Interpolate(const Point start, const Point end,
                                      const F32 progress) noexcept -> Point {
  return AnimationInterpolator<Point>::Interpolate(start, end, progress);
}

/// @brief Linearly interpolates two colors without clamping progress.
[[nodiscard]] inline auto Interpolate(const Color start, const Color end,
                                      const F32 progress) noexcept -> Color {
  return AnimationInterpolator<Color>::Interpolate(start, end, progress);
}
} // namespace NGIN::UI
