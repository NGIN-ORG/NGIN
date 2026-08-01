#pragma once

#include <NGIN/UI/Animation.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Style.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <optional>

namespace NGIN::UI {
/// @brief Translation and scale applied after layout without changing layout size.
struct MotionTransform final {
  Point translation{};
  Point scale{1.0F, 1.0F};

  [[nodiscard]] constexpr auto
  operator<=>(const MotionTransform &) const noexcept = default;
};

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
};

/// @brief Linearly interpolates two scalar values.
[[nodiscard]] auto Interpolate(F32 start, F32 end, F32 progress) noexcept
    -> F32;
/// @brief Linearly interpolates two points.
[[nodiscard]] auto Interpolate(Point start, Point end, F32 progress) noexcept
    -> Point;
/// @brief Linearly interpolates two RGBA colors.
[[nodiscard]] auto Interpolate(Color start, Color end, F32 progress) noexcept
    -> Color;
} // namespace NGIN::UI
