#include <NGIN/UI/UIRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>

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
[[nodiscard]] auto PackColor(const Color color) noexcept -> UInt32 {
  const auto channel = [](const F32 value) -> UInt32 {
    return static_cast<UInt32>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
  };
  return channel(color.red) | (channel(color.green) << 8U) |
         (channel(color.blue) << 16U) | (channel(color.alpha) << 24U);
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
  std::vector<Rect> clips{
      Rect{0.0F, 0.0F, static_cast<F32>(targetSize.width) / effectiveScale,
           static_cast<F32>(targetSize.height) / effectiveScale},
  };

  for (const auto &command : displayList) {
    std::visit(
        [&](const auto &value) {
          using Command = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Command, PushClipRect>) {
            clips.push_back(Intersect(clips.back(), value.rect));
          } else if constexpr (std::is_same_v<Command, PopClip>) {
            if (clips.size() > 1) {
              clips.pop_back();
            }
          } else if constexpr (std::is_same_v<Command, FillRect>) {
            const auto firstVertex =
                static_cast<UInt32>(packet.vertices.size());
            const auto firstIndex = static_cast<UInt32>(packet.indices.size());
            const auto color = PackColor(value.color);
            const auto left = value.rect.x * effectiveScale;
            const auto top = value.rect.y * effectiveScale;
            const auto right =
                (value.rect.x + value.rect.width) * effectiveScale;
            const auto bottom =
                (value.rect.y + value.rect.height) * effectiveScale;
            packet.vertices.insert(
                packet.vertices.end(),
                {
                    RenderVertex{left, top, 0.0F, 0.0F, color},
                    RenderVertex{right, top, 1.0F, 0.0F, color},
                    RenderVertex{right, bottom, 1.0F, 1.0F, color},
                    RenderVertex{left, bottom, 0.0F, 1.0F, color},
                });
            packet.indices.insert(packet.indices.end(),
                                  {firstVertex, firstVertex + 1,
                                   firstVertex + 2, firstVertex,
                                   firstVertex + 2, firstVertex + 3});

            const auto scissor =
                ToScissor(clips.back(), effectiveScale, targetSize);
            if (!packet.batches.empty() &&
                packet.batches.back().scissor == scissor &&
                !packet.batches.back().texture) {
              packet.batches.back().indexCount += 6;
            } else {
              packet.batches.push_back(RenderBatch{
                  .scissor = scissor,
                  .firstIndex = firstIndex,
                  .indexCount = 6,
              });
            }
          }
        },
        command);
  }
  return packet;
}
} // namespace NGIN::UI
