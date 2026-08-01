#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Geometry.hpp>

#include <limits>
#include <vector>

namespace NGIN::UI {
/// @brief Sizing rule used by one Grid row or column.
enum class GridTrackSizing : UInt8 {
  Fixed,
  Automatic,
  Weighted,
};

/// @brief Fixed, content-sized, or weighted Grid track with explicit bounds.
struct GridTrack final {
  GridTrackSizing sizing{GridTrackSizing::Weighted};
  F32 value{1.0F};
  F32 minimum{0.0F};
  F32 maximum{std::numeric_limits<F32>::infinity()};

  [[nodiscard]] static constexpr auto Fixed(const F32 size) noexcept
      -> GridTrack {
    return GridTrack{.sizing = GridTrackSizing::Fixed,
                     .value = size,
                     .minimum = size,
                     .maximum = size};
  }

  [[nodiscard]] static constexpr auto
  Auto(const F32 minimum = 0.0F,
       const F32 maximum = std::numeric_limits<F32>::infinity()) noexcept
      -> GridTrack {
    return GridTrack{.sizing = GridTrackSizing::Automatic,
                     .value = 0.0F,
                     .minimum = minimum,
                     .maximum = maximum};
  }

  [[nodiscard]] static constexpr auto
  Weighted(const F32 weight = 1.0F, const F32 minimum = 0.0F,
           const F32 maximum = std::numeric_limits<F32>::infinity()) noexcept
      -> GridTrack {
    return GridTrack{.sizing = GridTrackSizing::Weighted,
                     .value = weight,
                     .minimum = minimum,
                     .maximum = maximum};
  }
};

/// @brief Row, column, and spans used to place one Grid child.
struct GridPlacement final {
  UIntSize row{0};
  UIntSize column{0};
  UIntSize rowSpan{1};
  UIntSize columnSpan{1};
};

/// @brief Grid tracks and spacing authored on a Grid element.
struct GridProperties final {
  std::vector<GridTrack> rows{};
  std::vector<GridTrack> columns{};
  F32 rowGap{0.0F};
  F32 columnGap{0.0F};
};

/// @brief Main-axis direction used by WrapPanel.
enum class WrapOrientation : UInt8 {
  Horizontal,
  Vertical,
};

/// @brief Placement of items within each WrapPanel line.
enum class WrapLineAlignment : UInt8 {
  Start,
  Center,
  End,
  SpaceBetween,
};

/// @brief Direction, item gap, line gap, and line alignment for WrapPanel.
struct WrapPanelProperties final {
  WrapOrientation orientation{WrapOrientation::Horizontal};
  WrapLineAlignment lineAlignment{WrapLineAlignment::Start};
  F32 itemGap{0.0F};
  F32 lineGap{0.0F};
};

/// @brief Absolute child offset and desired-size participation in a Canvas.
struct CanvasPlacement final {
  Point offset{};
  bool contributesToDesiredSize{true};
};

/// @brief Bounded Canvas behavior authored on the Canvas element.
struct CanvasProperties final {
  bool clipToBounds{true};
};
} // namespace NGIN::UI
