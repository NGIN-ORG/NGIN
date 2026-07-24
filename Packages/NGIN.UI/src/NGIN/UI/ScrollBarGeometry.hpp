#pragma once

#include <NGIN/UI/RuntimeTree.hpp>

#include <algorithm>

namespace NGIN::UI::Detail {
struct ScrollBarGeometry final {
  Rect horizontalTrack{};
  Rect horizontalThumb{};
  Rect verticalTrack{};
  Rect verticalThumb{};
  bool hasHorizontal{false};
  bool hasVertical{false};
};

[[nodiscard]] inline auto ScrollThumb(const Rect track, const F32 viewport,
                                      const F32 content, const F32 offset,
                                      const F32 minimumLength,
                                      const bool horizontal) noexcept -> Rect {
  const auto trackLength = horizontal ? track.width : track.height;
  const auto viewportLength = std::max(0.0F, viewport);
  const auto contentLength = std::max(viewportLength, content);
  const auto maximumOffset = std::max(0.0F, contentLength - viewportLength);
  const auto thumbLength = std::clamp(
      trackLength * viewportLength / std::max(1.0F, contentLength),
      std::min(trackLength, std::max(4.0F, minimumLength)), trackLength);
  const auto travel = std::max(0.0F, trackLength - thumbLength);
  const auto position =
      maximumOffset > 0.0F
          ? travel * std::clamp(offset / maximumOffset, 0.0F, 1.0F)
          : 0.0F;
  return horizontal
             ? Rect{track.x + position, track.y, thumbLength, track.height}
             : Rect{track.x, track.y + position, track.width, thumbLength};
}

[[nodiscard]] inline auto ComputeScrollBars(const Rect bounds,
                                            const ScrollProperties &properties,
                                            const ScrollState &state) noexcept
    -> ScrollBarGeometry {
  ScrollBarGeometry result{};
  if (!properties.showScrollbars || bounds.width <= 0.0F ||
      bounds.height <= 0.0F) {
    return result;
  }

  result.hasHorizontal =
      properties.horizontal &&
      state.contentSize.width > state.viewportSize.width + 0.5F;
  result.hasVertical =
      properties.vertical &&
      state.contentSize.height > state.viewportSize.height + 0.5F;
  const auto thickness = std::clamp(properties.scrollbarThickness, 4.0F, 24.0F);
  constexpr F32 Inset = 2.0F;
  if (result.hasHorizontal) {
    result.horizontalTrack = Rect{
        bounds.x + Inset,
        bounds.y + bounds.height - thickness - Inset,
        std::max(0.0F, bounds.width - Inset * 2.0F -
                           (result.hasVertical ? thickness : 0.0F)),
        thickness,
    };
    result.horizontalThumb =
        ScrollThumb(result.horizontalTrack, state.viewportSize.width,
                    state.contentSize.width, state.offset.x,
                    properties.minimumThumbLength, true);
  }
  if (result.hasVertical) {
    result.verticalTrack = Rect{
        bounds.x + bounds.width - thickness - Inset,
        bounds.y + Inset,
        thickness,
        std::max(0.0F, bounds.height - Inset * 2.0F -
                           (result.hasHorizontal ? thickness : 0.0F)),
    };
    result.verticalThumb =
        ScrollThumb(result.verticalTrack, state.viewportSize.height,
                    state.contentSize.height, state.offset.y,
                    properties.minimumThumbLength, false);
  }
  return result;
}
} // namespace NGIN::UI::Detail
