#pragma once

#include <NGIN/UI/Rendering.hpp>

#include <vector>

namespace NGIN::UI::Testing {
/// @brief Texture upload captured by the recording render backend.
struct RecordedTextureUpdate final {
  TextureHandle texture{};
  PixelRect region{};
  UIntSize bytesPerRow{0};
  std::vector<Byte> bytes{};
};

/// @brief Texture allocation captured by the recording render backend.
struct RecordedTexture final {
  TextureHandle handle{};
  TextureCreateInfo info{};
  bool destroyed{false};
};

/// @brief Deep copy of a frame packet captured for assertions.
struct RecordedRenderPacket final {
  RenderSurfaceHandle surface{};
  std::vector<RenderVertex> vertices{};
  std::vector<UInt32> indices{};
  std::vector<RenderBatch> batches{};
  std::vector<RecordedTextureUpdate> textureUpdates{};
  PixelSize targetSize{};
  F32 scaleFactor{1.0F};
  Color clearColor{};
};

/// @brief Size, scale, and submitted frames recorded for one test surface.
struct RecordedSurface final {
  RenderSurfaceHandle handle{};
  PlatformWindowHandle window{};
  PixelSize size{};
  UIntSize renderCount{0};
  UIntSize presentCount{0};
  bool destroyed{false};
};

/// @brief Deterministic in-memory render backend for unit and integration tests.
class RecordingRenderBackend : public IRenderBackend {
public:
  [[nodiscard]] auto Name() const noexcept -> const char * override;
  [[nodiscard]] auto ContractVersion() const noexcept
      -> BackendContractVersion override;
  [[nodiscard]] auto Capabilities() const noexcept
      -> RenderCapabilityFlags override;
  auto Initialize(const RenderInitInfo &info) noexcept
      -> UIResult<void> override;
  auto CreateSurface(PlatformWindowHandle window,
                     PixelSize initialSize) noexcept
      -> UIResult<RenderSurfaceHandle> override;
  auto DestroySurface(RenderSurfaceHandle surface) noexcept
      -> UIResult<void> override;
  auto ResizeSurface(RenderSurfaceHandle surface, PixelSize size) noexcept
      -> UIResult<void> override;
  auto CreateTexture(const TextureCreateInfo &info) noexcept
      -> UIResult<TextureHandle> override;
  auto UpdateTexture(TextureHandle texture,
                     const TextureUpdateInfo &update) noexcept
      -> UIResult<void> override;
  auto DestroyTexture(TextureHandle texture) noexcept
      -> UIResult<void> override;
  auto Render(RenderSurfaceHandle surface, const RenderPacket &packet) noexcept
      -> UIResult<void> override;
  auto Present(RenderSurfaceHandle surface) noexcept -> UIResult<void> override;
  auto WaitIdle() noexcept -> UIResult<void> override;

  [[nodiscard]] auto IsInitialized() const noexcept -> bool;
  [[nodiscard]] auto ValidationEnabled() const noexcept -> bool;
  [[nodiscard]] auto WaitIdleCount() const noexcept -> UIntSize;
  [[nodiscard]] auto Surfaces() const noexcept
      -> const std::vector<RecordedSurface> &;
  [[nodiscard]] auto RenderPackets() const noexcept
      -> const std::vector<RecordedRenderPacket> &;
  [[nodiscard]] auto Textures() const noexcept
      -> const std::vector<RecordedTexture> &;
  [[nodiscard]] auto TextureUpdates() const noexcept
      -> const std::vector<RecordedTextureUpdate> &;

private:
  [[nodiscard]] auto FindSurface(RenderSurfaceHandle handle) noexcept
      -> RecordedSurface *;
  [[nodiscard]] auto FindTexture(TextureHandle handle) noexcept
      -> RecordedTexture *;

  bool m_initialized{false};
  bool m_validationEnabled{false};
  UInt32 m_nextSurfaceIndex{0};
  UInt32 m_nextTextureIndex{0};
  UIntSize m_waitIdleCount{0};
  std::vector<RecordedSurface> m_surfaces{};
  std::vector<RecordedTexture> m_textures{};
  std::vector<RecordedRenderPacket> m_renderPackets{};
  std::vector<RecordedTextureUpdate> m_textureUpdates{};
};
} // namespace NGIN::UI::Testing
