#pragma once

#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Rendering.hpp>

#include <vector>

namespace NGIN::UI {
struct PreparedRenderPacket final {
  std::vector<RenderVertex> vertices{};
  std::vector<UInt32> indices{};
  std::vector<RenderBatch> batches{};
  std::vector<TextureUpdate> textureUpdates{};
  PixelSize targetSize{};
  F32 scaleFactor{1.0F};
  Color clearColor{0.08F, 0.09F, 0.11F, 1.0F};

  [[nodiscard]] auto View() const noexcept -> RenderPacket;
};

class UIRenderer final {
public:
  [[nodiscard]] auto
  Build(const DisplayList &displayList, PixelSize targetSize, F32 scaleFactor,
        Color clearColor = Color{0.08F, 0.09F, 0.11F, 1.0F}) const
      -> PreparedRenderPacket;
};
} // namespace NGIN::UI
