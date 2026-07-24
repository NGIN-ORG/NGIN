#pragma once

#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <cmath>
#include <compare>
#include <limits>

namespace NGIN::UI {
/// @brief Device-independent scalar used by layout and authored geometry.
struct Dp final {
  F32 value{0.0F};

  constexpr Dp() noexcept = default;
  constexpr explicit Dp(F32 valueIn) noexcept : value(valueIn) {}

  [[nodiscard]] constexpr auto operator<=>(const Dp &) const noexcept = default;
};

/// @brief Physical-pixel scalar used at platform and render boundaries.
struct Px final {
  Int32 value{0};

  constexpr Px() noexcept = default;
  constexpr explicit Px(Int32 valueIn) noexcept : value(valueIn) {}

  [[nodiscard]] constexpr auto operator<=>(const Px &) const noexcept = default;
};

/// @brief Fractional percentage scalar used for relative dimensions.
struct Percent final {
  F32 value{0.0F};

  constexpr Percent() noexcept = default;
  constexpr explicit Percent(F32 valueIn) noexcept : value(valueIn) {}
};

namespace Units {
[[nodiscard]] consteval auto operator""_dp(long double value) noexcept -> Dp {
  return Dp{static_cast<F32>(value)};
}

[[nodiscard]] consteval auto operator""_dp(unsigned long long value) noexcept
    -> Dp {
  return Dp{static_cast<F32>(value)};
}

[[nodiscard]] consteval auto operator""_percent(long double value) noexcept
    -> Percent {
  return Percent{static_cast<F32>(value / 100.0L)};
}

[[nodiscard]] consteval auto
operator""_percent(unsigned long long value) noexcept -> Percent {
  return Percent{static_cast<F32>(value) / 100.0F};
}
} // namespace Units

/// @brief Two-dimensional point in device-independent coordinates.
struct Point final {
  F32 x{0.0F};
  F32 y{0.0F};

  [[nodiscard]] constexpr auto
  operator<=>(const Point &) const noexcept = default;
};

/// @brief Width and height in device-independent coordinates.
struct Size final {
  F32 width{0.0F};
  F32 height{0.0F};

  [[nodiscard]] constexpr auto
  operator<=>(const Size &) const noexcept = default;
};

/// @brief Axis-aligned rectangle in device-independent coordinates.
struct Rect final {
  F32 x{0.0F};
  F32 y{0.0F};
  F32 width{0.0F};
  F32 height{0.0F};

  [[nodiscard]] constexpr auto Contains(const Point point) const noexcept
      -> bool {
    return point.x >= x && point.y >= y && point.x < x + width &&
           point.y < y + height;
  }

  [[nodiscard]] constexpr auto
  operator<=>(const Rect &) const noexcept = default;
};

/// @brief Per-edge device-independent thickness or inset.
struct Thickness final {
  F32 left{0.0F};
  F32 top{0.0F};
  F32 right{0.0F};
  F32 bottom{0.0F};

  [[nodiscard]] static constexpr auto Uniform(const Dp value) noexcept
      -> Thickness {
    return Thickness{value.value, value.value, value.value, value.value};
  }

  [[nodiscard]] constexpr auto
  operator<=>(const Thickness &) const noexcept = default;
};

/// @brief Per-corner radius for rounded rectangles.
struct CornerRadius final {
  F32 topLeft{0.0F};
  F32 topRight{0.0F};
  F32 bottomRight{0.0F};
  F32 bottomLeft{0.0F};

  [[nodiscard]] static constexpr auto Uniform(const Dp value) noexcept
      -> CornerRadius {
    return CornerRadius{value.value, value.value, value.value, value.value};
  }

  [[nodiscard]] constexpr auto
  operator<=>(const CornerRadius &) const noexcept = default;
};

/// @brief Two-dimensional point in physical pixels.
struct PixelPoint final {
  Int32 x{0};
  Int32 y{0};

  [[nodiscard]] constexpr auto
  operator<=>(const PixelPoint &) const noexcept = default;
};

/// @brief Width and height in physical pixels.
struct PixelSize final {
  UInt32 width{0};
  UInt32 height{0};

  [[nodiscard]] constexpr auto IsEmpty() const noexcept -> bool {
    return width == 0 || height == 0;
  }

  [[nodiscard]] constexpr auto
  operator<=>(const PixelSize &) const noexcept = default;
};

/// @brief Axis-aligned rectangle in physical pixels.
struct PixelRect final {
  Int32 x{0};
  Int32 y{0};
  UInt32 width{0};
  UInt32 height{0};

  [[nodiscard]] constexpr auto
  operator<=>(const PixelRect &) const noexcept = default;
};

/// @brief Minimum and maximum size bounds supplied during measurement.
struct SizeConstraints final {
  Size minimum{};
  Size maximum{
      std::numeric_limits<F32>::infinity(),
      std::numeric_limits<F32>::infinity(),
  };

  [[nodiscard]] constexpr auto Constrain(const Size requested) const noexcept
      -> Size {
    return Size{
        std::clamp(requested.width, minimum.width, maximum.width),
        std::clamp(requested.height, minimum.height, maximum.height),
    };
  }
};

[[nodiscard]] inline auto ToPixels(const Dp value,
                                   const F32 scaleFactor) noexcept -> Px {
  return Px{static_cast<Int32>(std::lround(value.value * scaleFactor))};
}

[[nodiscard]] inline auto ToPixelSize(const Size value,
                                      const F32 scaleFactor) noexcept
    -> PixelSize {
  const auto width = std::max(0.0F, std::round(value.width * scaleFactor));
  const auto height = std::max(0.0F, std::round(value.height * scaleFactor));
  return PixelSize{
      static_cast<UInt32>(width),
      static_cast<UInt32>(height),
  };
}

[[nodiscard]] inline auto ToPixelRect(const Rect value,
                                      const F32 scaleFactor) noexcept
    -> PixelRect {
  const auto originX = std::round(value.x * scaleFactor);
  const auto originY = std::round(value.y * scaleFactor);
  const auto width = std::max(0.0F, std::round(value.width * scaleFactor));
  const auto height = std::max(0.0F, std::round(value.height * scaleFactor));
  return PixelRect{
      static_cast<Int32>(originX),
      static_cast<Int32>(originY),
      static_cast<UInt32>(width),
      static_cast<UInt32>(height),
  };
}
} // namespace NGIN::UI
