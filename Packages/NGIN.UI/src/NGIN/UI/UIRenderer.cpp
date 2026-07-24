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
                const Rect textureRect = Rect{0.0F, 0.0F, 1.0F, 1.0F}) {
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
    packet.vertices.push_back(RenderVertex{
        transformed.x * scaleFactor,
        transformed.y * scaleFactor,
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

void AppendRoundedRect(PreparedRenderPacket &packet, const Rect rect,
                       const CornerRadius radii, const Transform2D transform,
                       const F32 scaleFactor, const UInt32 color,
                       const PixelRect scissor) {
  if (rect.width <= 0.0F || rect.height <= 0.0F || scissor.width == 0 ||
      scissor.height == 0) {
    return;
  }

  constexpr UIntSize segmentsPerCorner = 6;
  constexpr F32 halfPi = std::numbers::pi_v<F32> * 0.5F;
  const auto maximumRadius = std::min(rect.width, rect.height) * 0.5F;
  const std::array radiiClamped{
      std::clamp(radii.topLeft, 0.0F, maximumRadius),
      std::clamp(radii.topRight, 0.0F, maximumRadius),
      std::clamp(radii.bottomRight, 0.0F, maximumRadius),
      std::clamp(radii.bottomLeft, 0.0F, maximumRadius),
  };
  const std::array centers{
      Point{rect.x + radiiClamped[0], rect.y + radiiClamped[0]},
      Point{rect.x + rect.width - radiiClamped[1], rect.y + radiiClamped[1]},
      Point{rect.x + rect.width - radiiClamped[2],
            rect.y + rect.height - radiiClamped[2]},
      Point{rect.x + radiiClamped[3], rect.y + rect.height - radiiClamped[3]},
  };
  const std::array startAngles{
      std::numbers::pi_v<F32>,
      -halfPi,
      0.0F,
      halfPi,
  };

  const auto firstVertex = static_cast<UInt32>(packet.vertices.size());
  const auto firstIndex = static_cast<UInt32>(packet.indices.size());
  const auto center = TransformPoint(
      Point{rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F},
      transform);
  packet.vertices.push_back(RenderVertex{
      center.x * scaleFactor,
      center.y * scaleFactor,
      0.5F,
      0.5F,
      color,
  });

  for (UIntSize corner = 0; corner < centers.size(); ++corner) {
    for (UIntSize segment = 0; segment <= segmentsPerCorner; ++segment) {
      const auto angle =
          startAngles[corner] + halfPi * static_cast<F32>(segment) /
                                    static_cast<F32>(segmentsPerCorner);
      const auto point = TransformPoint(
          Point{
              centers[corner].x + std::cos(angle) * radiiClamped[corner],
              centers[corner].y + std::sin(angle) * radiiClamped[corner],
          },
          transform);
      packet.vertices.push_back(RenderVertex{
          point.x * scaleFactor,
          point.y * scaleFactor,
          0.0F,
          0.0F,
          color,
      });
    }
  }

  const auto perimeterCount =
      static_cast<UInt32>(centers.size() * (segmentsPerCorner + 1));
  for (UInt32 index = 0; index < perimeterCount; ++index) {
    packet.indices.insert(packet.indices.end(),
                          {firstVertex, firstVertex + 1 + index,
                           firstVertex + 1 + ((index + 1) % perimeterCount)});
  }
  AppendBatch(packet, {}, scissor, firstIndex, perimeterCount * 3);
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
            AppendQuad(packet, value.rect, transforms.back(), effectiveScale,
                       PackColor(value.color, opacities.back()),
                       ToScissor(clips.back(), effectiveScale, targetSize));
          } else if constexpr (std::is_same_v<Command, FillRoundedRect>) {
            AppendRoundedRect(
                packet, value.rect, value.radius, transforms.back(),
                effectiveScale, PackColor(value.color, opacities.back()),
                ToScissor(clips.back(), effectiveScale, targetSize));
          } else if constexpr (std::is_same_v<Command, StrokeRect>) {
            const auto thickness = std::clamp(
                value.thickness, 0.0F,
                std::min(value.rect.width, value.rect.height) * 0.5F);
            const auto color = PackColor(value.color, opacities.back());
            const auto scissor =
                ToScissor(clips.back(), effectiveScale, targetSize);
            AppendQuad(
                packet,
                Rect{value.rect.x, value.rect.y, value.rect.width, thickness},
                transforms.back(), effectiveScale, color, scissor);
            AppendQuad(packet,
                       Rect{value.rect.x,
                            value.rect.y + value.rect.height - thickness,
                            value.rect.width, thickness},
                       transforms.back(), effectiveScale, color, scissor);
            AppendQuad(packet,
                       Rect{value.rect.x, value.rect.y + thickness, thickness,
                            value.rect.height - thickness * 2.0F},
                       transforms.back(), effectiveScale, color, scissor);
            AppendQuad(packet,
                       Rect{value.rect.x + value.rect.width - thickness,
                            value.rect.y + thickness, thickness,
                            value.rect.height - thickness * 2.0F},
                       transforms.back(), effectiveScale, color, scissor);
          } else if constexpr (std::is_same_v<Command, DrawImage>) {
            if (value.texture) {
              AppendQuad(packet, value.destination, transforms.back(),
                         effectiveScale,
                         PackColor(value.tint, opacities.back()),
                         ToScissor(clips.back(), effectiveScale, targetSize),
                         value.texture);
            }
          } else if constexpr (std::is_same_v<Command, DrawGlyphRun>) {
            if (value.atlas) {
              const auto color = PackColor(value.color, opacities.back());
              const auto scissor =
                  ToScissor(clips.back(), effectiveScale, targetSize);
              for (const auto &glyph : value.glyphs) {
                AppendQuad(packet, glyph.destination, transforms.back(),
                           effectiveScale, color, scissor, value.atlas,
                           glyph.textureCoordinates);
              }
            }
          }
        },
        command);
  }
  return packet;
}
} // namespace NGIN::UI
