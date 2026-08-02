#include <NGIN/UI/UIRenderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <vector>

namespace NGIN::UI {
auto PreparedRenderPacket::View() const noexcept -> RenderPacket {
  return RenderPacket{
      .vertices = vertices,
      .indices = indices,
      .batches = batches,
      .textureUpdates = textureUpdates,
      .targetSize = targetSize,
      .scaleFactor = scaleFactor,
      .clearColor = clearColor,
  };
}

namespace {
struct Transform2D final {
  F32 translateX{0.0F};
  F32 translateY{0.0F};
  F32 scaleX{1.0F};
  F32 scaleY{1.0F};
};

struct PixelRadius final {
  F32 x{0.0F};
  F32 y{0.0F};
};

struct PixelRoundedRect final {
  Rect rect{};
  std::array<PixelRadius, 4> radii{};
};

constexpr UIntSize MaximumRoundedSegments = 16;

struct PixelContour final {
  std::array<Point, 4 * (MaximumRoundedSegments + 1)> points{};
  UIntSize count{0};
};

[[nodiscard]] auto Compose(const Transform2D parent,
                           const PushTransform local) noexcept -> Transform2D {
  return Transform2D{
      .translateX = parent.translateX + local.translateX * parent.scaleX,
      .translateY = parent.translateY + local.translateY * parent.scaleY,
      .scaleX = parent.scaleX * local.scaleX,
      .scaleY = parent.scaleY * local.scaleY,
  };
}

[[nodiscard]] auto TransformPoint(const Point point,
                                  const Transform2D transform) noexcept
    -> Point {
  return Point{
      point.x * transform.scaleX + transform.translateX,
      point.y * transform.scaleY + transform.translateY,
  };
}

[[nodiscard]] auto TransformRect(const Rect rect,
                                 const Transform2D transform) noexcept -> Rect {
  const auto first = TransformPoint(Point{rect.x, rect.y}, transform);
  const auto second = TransformPoint(
      Point{rect.x + rect.width, rect.y + rect.height}, transform);
  return Rect{
      std::min(first.x, second.x),
      std::min(first.y, second.y),
      std::abs(second.x - first.x),
      std::abs(second.y - first.y),
  };
}

[[nodiscard]] auto PackColor(const Color color, const F32 opacity) noexcept
    -> UInt32 {
  const auto alpha = std::clamp(color.alpha * opacity, 0.0F, 1.0F);
  const auto channel = [](const F32 value) -> UInt32 {
    return static_cast<UInt32>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
  };
  return channel(color.red * alpha) | (channel(color.green * alpha) << 8U) |
         (channel(color.blue * alpha) << 16U) | (channel(alpha) << 24U);
}

[[nodiscard]] auto ToScissor(const Rect rect, const F32 scaleFactor,
                             const PixelSize target) noexcept -> PixelRect {
  const auto left =
      std::clamp(static_cast<Int32>(std::floor(rect.x * scaleFactor)), 0,
                 static_cast<Int32>(target.width));
  const auto top =
      std::clamp(static_cast<Int32>(std::floor(rect.y * scaleFactor)), 0,
                 static_cast<Int32>(target.height));
  const auto right = std::clamp(
      static_cast<Int32>(std::ceil((rect.x + rect.width) * scaleFactor)), left,
      static_cast<Int32>(target.width));
  const auto bottom = std::clamp(
      static_cast<Int32>(std::ceil((rect.y + rect.height) * scaleFactor)), top,
      static_cast<Int32>(target.height));
  return PixelRect{left, top, static_cast<UInt32>(right - left),
                   static_cast<UInt32>(bottom - top)};
}

[[nodiscard]] auto Intersect(const Rect left, const Rect right) noexcept
    -> Rect {
  const auto x = std::max(left.x, right.x);
  const auto y = std::max(left.y, right.y);
  const auto farX = std::min(left.x + left.width, right.x + right.width);
  const auto farY = std::min(left.y + left.height, right.y + right.height);
  return Rect{x, y, std::max(0.0F, farX - x), std::max(0.0F, farY - y)};
}

void AppendBatch(PreparedRenderPacket &packet, const TextureHandle texture,
                 const PixelRect scissor, const UInt32 firstIndex,
                 const UInt32 indexCount) {
  if (!packet.batches.empty() && packet.batches.back().texture == texture &&
      packet.batches.back().scissor == scissor &&
      packet.batches.back().firstIndex + packet.batches.back().indexCount ==
          firstIndex) {
    packet.batches.back().indexCount += indexCount;
    return;
  }
  packet.batches.push_back(RenderBatch{
      .texture = texture,
      .scissor = scissor,
      .firstIndex = firstIndex,
      .indexCount = indexCount,
  });
}

void AppendQuad(PreparedRenderPacket &packet, const Rect rect,
                const Transform2D transform, const F32 scaleFactor,
                const UInt32 color, const PixelRect scissor,
                const TextureHandle texture = {},
                const Rect textureRect = Rect{0.0F, 0.0F, 1.0F, 1.0F},
                const bool snapToPixels = false) {
  if (rect.width <= 0.0F || rect.height <= 0.0F || scissor.width == 0 ||
      scissor.height == 0) {
    return;
  }

  const std::array logicalPoints{
      Point{rect.x, rect.y},
      Point{rect.x + rect.width, rect.y},
      Point{rect.x + rect.width, rect.y + rect.height},
      Point{rect.x, rect.y + rect.height},
  };
  const std::array textureCoordinates{
      Point{textureRect.x, textureRect.y},
      Point{textureRect.x + textureRect.width, textureRect.y},
      Point{textureRect.x + textureRect.width,
            textureRect.y + textureRect.height},
      Point{textureRect.x, textureRect.y + textureRect.height},
  };

  const auto firstVertex = static_cast<UInt32>(packet.vertices.size());
  const auto firstIndex = static_cast<UInt32>(packet.indices.size());
  for (UIntSize index = 0; index < logicalPoints.size(); ++index) {
    const auto transformed = TransformPoint(logicalPoints[index], transform);
    const auto pixelX = transformed.x * scaleFactor;
    const auto pixelY = transformed.y * scaleFactor;
    packet.vertices.push_back(RenderVertex{
        snapToPixels ? std::round(pixelX) : pixelX,
        snapToPixels ? std::round(pixelY) : pixelY,
        textureCoordinates[index].x,
        textureCoordinates[index].y,
        color,
    });
  }
  packet.indices.insert(packet.indices.end(),
                        {firstVertex, firstVertex + 1, firstVertex + 2,
                         firstVertex, firstVertex + 2, firstVertex + 3});
  AppendBatch(packet, texture, scissor, firstIndex, 6);
}

[[nodiscard]] auto ToPixelRoundedRect(const Rect rect, const CornerRadius radii,
                                      const Transform2D transform,
                                      const F32 scaleFactor) noexcept
    -> PixelRoundedRect {
  const auto transformed = TransformRect(rect, transform);
  const auto maximumRadius = std::min(rect.width, rect.height) * 0.5F;
  std::array logicalRadii{
      std::clamp(radii.topLeft, 0.0F, maximumRadius),
      std::clamp(radii.topRight, 0.0F, maximumRadius),
      std::clamp(radii.bottomRight, 0.0F, maximumRadius),
      std::clamp(radii.bottomLeft, 0.0F, maximumRadius),
  };
  if (transform.scaleX < 0.0F) {
    std::swap(logicalRadii[0], logicalRadii[1]);
    std::swap(logicalRadii[2], logicalRadii[3]);
  }
  if (transform.scaleY < 0.0F) {
    std::swap(logicalRadii[0], logicalRadii[3]);
    std::swap(logicalRadii[1], logicalRadii[2]);
  }

  const auto radiusScaleX = std::abs(transform.scaleX) * scaleFactor;
  const auto radiusScaleY = std::abs(transform.scaleY) * scaleFactor;
  PixelRoundedRect result{
      .rect =
          Rect{
              transformed.x * scaleFactor,
              transformed.y * scaleFactor,
              transformed.width * scaleFactor,
              transformed.height * scaleFactor,
          },
  };
  for (UIntSize index = 0; index < logicalRadii.size(); ++index) {
    result.radii[index] = PixelRadius{
        logicalRadii[index] * radiusScaleX,
        logicalRadii[index] * radiusScaleY,
    };
  }
  return result;
}

[[nodiscard]] auto
HasRoundedCorners(const std::array<PixelRadius, 4> &radii) noexcept -> bool {
  return std::any_of(radii.begin(), radii.end(), [](const PixelRadius radius) {
    return radius.x > 0.0001F && radius.y > 0.0001F;
  });
}

[[nodiscard]] auto
RoundedSegments(const std::array<PixelRadius, 4> &radii) noexcept -> UIntSize {
  constexpr F32 curveTolerance = 0.2F;
  constexpr UIntSize minimumSegments = 4;
  F32 maximumRadius = 0.0F;
  for (const auto radius : radii) {
    maximumRadius = std::max({maximumRadius, radius.x, radius.y});
  }
  if (maximumRadius <= curveTolerance) {
    return minimumSegments;
  }
  const auto angle =
      2.0F *
      std::acos(std::clamp(1.0F - curveTolerance / maximumRadius, -1.0F, 1.0F));
  if (angle <= 0.0001F) {
    return MaximumRoundedSegments;
  }
  return std::clamp(static_cast<UIntSize>(
                        std::ceil((std::numbers::pi_v<F32> * 0.5F) / angle)),
                    minimumSegments, MaximumRoundedSegments);
}

[[nodiscard]] auto RoundedContour(const PixelRoundedRect &shape,
                                  const F32 offset,
                                  const UIntSize segmentsPerCorner,
                                  const bool preserveCornerSamples)
    -> PixelContour {
  const Rect rect{
      shape.rect.x - offset,
      shape.rect.y - offset,
      shape.rect.width + offset * 2.0F,
      shape.rect.height + offset * 2.0F,
  };
  PixelContour result{};
  if (!preserveCornerSamples) {
    result.points[0] = Point{rect.x, rect.y};
    result.points[1] = Point{rect.x + rect.width, rect.y};
    result.points[2] = Point{rect.x + rect.width, rect.y + rect.height};
    result.points[3] = Point{rect.x, rect.y + rect.height};
    result.count = 4;
    return result;
  }

  std::array<PixelRadius, 4> radii{};
  for (UIntSize index = 0; index < radii.size(); ++index) {
    radii[index] = PixelRadius{
        shape.radii[index].x > 0.0F
            ? std::clamp(shape.radii[index].x + offset, 0.0F, rect.width * 0.5F)
            : 0.0F,
        shape.radii[index].y > 0.0F ? std::clamp(shape.radii[index].y + offset,
                                                 0.0F, rect.height * 0.5F)
                                    : 0.0F,
    };
  }
  const std::array centers{
      Point{rect.x + radii[0].x, rect.y + radii[0].y},
      Point{rect.x + rect.width - radii[1].x, rect.y + radii[1].y},
      Point{rect.x + rect.width - radii[2].x,
            rect.y + rect.height - radii[2].y},
      Point{rect.x + radii[3].x, rect.y + rect.height - radii[3].y},
  };
  constexpr F32 halfPi = std::numbers::pi_v<F32> * 0.5F;
  const std::array startAngles{
      std::numbers::pi_v<F32>,
      -halfPi,
      0.0F,
      halfPi,
  };

  for (UIntSize corner = 0; corner < centers.size(); ++corner) {
    for (UIntSize segment = 0; segment <= segmentsPerCorner; ++segment) {
      const auto angle =
          startAngles[corner] + halfPi * static_cast<F32>(segment) /
                                    static_cast<F32>(segmentsPerCorner);
      result.points[result.count++] = Point{
          centers[corner].x + std::cos(angle) * radii[corner].x,
          centers[corner].y + std::sin(angle) * radii[corner].y,
      };
    }
  }
  return result;
}

[[nodiscard]] auto AppendContour(PreparedRenderPacket &packet,
                                 const PixelContour &contour,
                                 const UInt32 color) -> UInt32 {
  const auto firstVertex = static_cast<UInt32>(packet.vertices.size());
  for (UIntSize index = 0; index < contour.count; ++index) {
    const auto point = contour.points[index];
    packet.vertices.push_back(RenderVertex{
        point.x,
        point.y,
        0.0F,
        0.0F,
        color,
    });
  }
  return firstVertex;
}

void AppendRingIndices(PreparedRenderPacket &packet, const UInt32 outer,
                       const UInt32 inner, const UInt32 count) {
  for (UInt32 index = 0; index < count; ++index) {
    const auto next = (index + 1) % count;
    packet.indices.insert(packet.indices.end(),
                          {outer + index, outer + next, inner + next,
                           outer + index, inner + next, inner + index});
  }
}

void AppendAntialiasedFill(PreparedRenderPacket &packet, const Rect rect,
                           const CornerRadius radii,
                           const Transform2D transform, const F32 scaleFactor,
                           const UInt32 color, const PixelRect scissor) {
  if (rect.width <= 0.0F || rect.height <= 0.0F || scissor.width == 0 ||
      scissor.height == 0) {
    return;
  }

  const auto shape = ToPixelRoundedRect(rect, radii, transform, scaleFactor);
  if (shape.rect.width <= 0.0F || shape.rect.height <= 0.0F) {
    return;
  }
  const auto coverageHalfWidth =
      std::min(0.5F, std::min(shape.rect.width, shape.rect.height) * 0.25F);
  const auto rounded = HasRoundedCorners(shape.radii);
  const auto segments = RoundedSegments(shape.radii);
  const auto inner =
      RoundedContour(shape, -coverageHalfWidth, segments, rounded);
  const auto outer =
      RoundedContour(shape, coverageHalfWidth, segments, rounded);
  const auto firstIndex = static_cast<UInt32>(packet.indices.size());
  const auto center = static_cast<UInt32>(packet.vertices.size());
  packet.vertices.push_back(RenderVertex{
      shape.rect.x + shape.rect.width * 0.5F,
      shape.rect.y + shape.rect.height * 0.5F,
      0.0F,
      0.0F,
      color,
  });
  const auto innerStart = AppendContour(packet, inner, color);
  const auto outerStart = AppendContour(packet, outer, 0U);
  const auto count = static_cast<UInt32>(inner.count);
  for (UInt32 index = 0; index < count; ++index) {
    packet.indices.insert(
        packet.indices.end(),
        {center, innerStart + index, innerStart + ((index + 1) % count)});
  }
  AppendRingIndices(packet, innerStart, outerStart, count);
  AppendBatch(packet, {}, scissor, firstIndex,
              static_cast<UInt32>(packet.indices.size()) - firstIndex);
}

void AppendAntialiasedStroke(PreparedRenderPacket &packet, const Rect rect,
                             const CornerRadius radii, const F32 thickness,
                             const Transform2D transform, const F32 scaleFactor,
                             const UInt32 color, const PixelRect scissor) {
  if (rect.width <= 0.0F || rect.height <= 0.0F || thickness <= 0.0F ||
      scissor.width == 0 || scissor.height == 0) {
    return;
  }

  const auto clampedThickness =
      std::clamp(thickness, 0.0F, std::min(rect.width, rect.height) * 0.5F);
  if (clampedThickness * 2.0F >= std::min(rect.width, rect.height)) {
    AppendAntialiasedFill(packet, rect, radii, transform, scaleFactor, color,
                          scissor);
    return;
  }

  const auto outerShape =
      ToPixelRoundedRect(rect, radii, transform, scaleFactor);
  const auto insetX =
      clampedThickness * std::abs(transform.scaleX) * scaleFactor;
  const auto insetY =
      clampedThickness * std::abs(transform.scaleY) * scaleFactor;
  if (outerShape.rect.width <= 0.0F || outerShape.rect.height <= 0.0F ||
      insetX <= 0.0F || insetY <= 0.0F) {
    return;
  }
  PixelRoundedRect innerShape{
      .rect =
          Rect{
              outerShape.rect.x + insetX,
              outerShape.rect.y + insetY,
              outerShape.rect.width - insetX * 2.0F,
              outerShape.rect.height - insetY * 2.0F,
          },
  };
  for (UIntSize index = 0; index < innerShape.radii.size(); ++index) {
    innerShape.radii[index] = PixelRadius{
        std::max(0.0F, outerShape.radii[index].x - insetX),
        std::max(0.0F, outerShape.radii[index].y - insetY),
    };
  }
  const auto coverageHalfWidth = std::min(
      {0.5F, std::min(insetX, insetY) * 0.5F,
       std::min(outerShape.rect.width, outerShape.rect.height) * 0.25F});
  const auto rounded = HasRoundedCorners(outerShape.radii);
  const auto segments = RoundedSegments(outerShape.radii);
  const auto outerTransparent =
      RoundedContour(outerShape, coverageHalfWidth, segments, rounded);
  const auto outerFull =
      RoundedContour(outerShape, -coverageHalfWidth, segments, rounded);
  const auto innerFull =
      RoundedContour(innerShape, coverageHalfWidth, segments, rounded);
  const auto innerTransparent =
      RoundedContour(innerShape, -coverageHalfWidth, segments, rounded);
  const auto firstIndex = static_cast<UInt32>(packet.indices.size());
  const auto outerTransparentStart =
      AppendContour(packet, outerTransparent, 0U);
  const auto outerFullStart = AppendContour(packet, outerFull, color);
  const auto innerFullStart = AppendContour(packet, innerFull, color);
  const auto innerTransparentStart =
      AppendContour(packet, innerTransparent, 0U);
  const auto count = static_cast<UInt32>(outerFull.count);
  AppendRingIndices(packet, outerTransparentStart, outerFullStart, count);
  AppendRingIndices(packet, outerFullStart, innerFullStart, count);
  AppendRingIndices(packet, innerFullStart, innerTransparentStart, count);
  AppendBatch(packet, {}, scissor, firstIndex,
              static_cast<UInt32>(packet.indices.size()) - firstIndex);
}
} // namespace

auto UIRenderer::Build(const DisplayList &displayList,
                       const PixelSize targetSize, const F32 scaleFactor,
                       const Color clearColor) const -> PreparedRenderPacket {
  const auto effectiveScale = std::max(scaleFactor, 0.001F);
  PreparedRenderPacket packet{
      .targetSize = targetSize,
      .scaleFactor = effectiveScale,
      .clearColor = clearColor,
  };
  std::vector<Transform2D> transforms{Transform2D{}};
  std::vector<Rect> clips{
      Rect{0.0F, 0.0F, static_cast<F32>(targetSize.width) / effectiveScale,
           static_cast<F32>(targetSize.height) / effectiveScale},
  };
  std::vector<F32> opacities{1.0F};

  for (const auto &command : displayList) {
    std::visit(
        [&](const auto &value) {
          using Command = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Command, PushClipRect>) {
            clips.push_back(Intersect(
                clips.back(), TransformRect(value.rect, transforms.back())));
          } else if constexpr (std::is_same_v<Command, PopClip>) {
            if (clips.size() > 1) {
              clips.pop_back();
            }
          } else if constexpr (std::is_same_v<Command, PushTransform>) {
            transforms.push_back(Compose(transforms.back(), value));
          } else if constexpr (std::is_same_v<Command, PopTransform>) {
            if (transforms.size() > 1) {
              transforms.pop_back();
            }
          } else if constexpr (std::is_same_v<Command, BeginOpacityLayer>) {
            opacities.push_back(opacities.back() *
                                std::clamp(value.opacity, 0.0F, 1.0F));
          } else if constexpr (std::is_same_v<Command, EndOpacityLayer>) {
            if (opacities.size() > 1) {
              opacities.pop_back();
            }
          } else if constexpr (std::is_same_v<Command, FillRect>) {
            AppendAntialiasedFill(
                packet, value.rect, CornerRadius{}, transforms.back(),
                effectiveScale, PackColor(value.color, opacities.back()),
                ToScissor(clips.back(), effectiveScale, targetSize));
          } else if constexpr (std::is_same_v<Command, FillRoundedRect>) {
            AppendAntialiasedFill(
                packet, value.rect, value.radius, transforms.back(),
                effectiveScale, PackColor(value.color, opacities.back()),
                ToScissor(clips.back(), effectiveScale, targetSize));
          } else if constexpr (std::is_same_v<Command, StrokeRect>) {
            AppendAntialiasedStroke(
                packet, value.rect, CornerRadius{}, value.thickness,
                transforms.back(), effectiveScale,
                PackColor(value.color, opacities.back()),
                ToScissor(clips.back(), effectiveScale, targetSize));
          } else if constexpr (std::is_same_v<Command, StrokeRoundedRect>) {
            AppendAntialiasedStroke(
                packet, value.rect, value.radius, value.thickness,
                transforms.back(), effectiveScale,
                PackColor(value.color, opacities.back()),
                ToScissor(clips.back(), effectiveScale, targetSize));
          } else if constexpr (std::is_same_v<Command, DrawImage>) {
            if (value.texture) {
              AppendQuad(packet, value.destination, transforms.back(),
                         effectiveScale,
                         PackColor(value.tint, opacities.back()),
                         ToScissor(clips.back(), effectiveScale, targetSize),
                         value.texture, value.textureCoordinates);
            }
          } else if constexpr (std::is_same_v<Command, DrawGlyphRun>) {
            if (value.atlas) {
              const auto color = PackColor(value.color, opacities.back());
              const auto scissor =
                  ToScissor(clips.back(), effectiveScale, targetSize);
              for (const auto &glyph : value.glyphs) {
                AppendQuad(packet, glyph.destination, transforms.back(),
                           effectiveScale, color, scissor, value.atlas,
                           glyph.textureCoordinates, true);
              }
            }
          }
        },
        command);
  }
  return packet;
}
} // namespace NGIN::UI
