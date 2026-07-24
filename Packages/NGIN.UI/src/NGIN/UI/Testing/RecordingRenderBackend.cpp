#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>

#include <algorithm>

namespace NGIN::UI::Testing {
namespace {
[[nodiscard]] auto CopyTextureUpdate(const TextureHandle texture,
                                     const TextureUpdateInfo &update)
    -> RecordedTextureUpdate {
  return RecordedTextureUpdate{
      .texture = texture,
      .region = update.region,
      .bytesPerRow = update.bytesPerRow,
      .bytes = std::vector<Byte>{update.bytes.begin(), update.bytes.end()},
  };
}
} // namespace

auto RecordingRenderBackend::Name() const noexcept -> const char * {
  return "RecordingRenderer";
}

auto RecordingRenderBackend::ContractVersion() const noexcept
    -> BackendContractVersion {
  return CurrentBackendContractVersion;
}

auto RecordingRenderBackend::Capabilities() const noexcept
    -> RenderCapabilityFlags {
  return RenderCapabilityFlags::TextureUpdates |
         RenderCapabilityFlags::ScissorRects | RenderCapabilityFlags::Index32;
}

auto RecordingRenderBackend::Initialize(const RenderInitInfo &info) noexcept
    -> UIResult<void> {
  m_initialized = true;
  m_validationEnabled = info.enableValidation;
  return {};
}

auto RecordingRenderBackend::CreateSurface(const PlatformWindowHandle window,
                                           const PixelSize initialSize) noexcept
    -> UIResult<RenderSurfaceHandle> {
  if (!m_initialized) {
    return MakeUIError(UIErrorCode::BackendUnavailable,
                       "Recording renderer is not initialized", Name(),
                       "CreateSurface");
  }
  if (!window || initialSize.IsEmpty()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "A valid window and non-zero size are required", Name(),
                       "CreateSurface");
  }

  const RenderSurfaceHandle handle{m_nextSurfaceIndex++, 1};
  m_surfaces.push_back(RecordedSurface{
      .handle = handle,
      .window = window,
      .size = initialSize,
  });
  return handle;
}

auto RecordingRenderBackend::DestroySurface(
    const RenderSurfaceHandle surface) noexcept -> UIResult<void> {
  auto *record = FindSurface(surface);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown render surface",
                       Name(), "DestroySurface");
  }
  record->destroyed = true;
  return {};
}

auto RecordingRenderBackend::ResizeSurface(const RenderSurfaceHandle surface,
                                           const PixelSize size) noexcept
    -> UIResult<void> {
  auto *record = FindSurface(surface);
  if (record == nullptr || record->destroyed || size.IsEmpty()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "A live surface and non-zero size are required", Name(),
                       "ResizeSurface");
  }
  record->size = size;
  return {};
}

auto RecordingRenderBackend::CreateTexture(
    const TextureCreateInfo &info) noexcept -> UIResult<TextureHandle> {
  if (!m_initialized || info.size.IsEmpty()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "A non-zero texture size is required", Name(),
                       "CreateTexture");
  }
  const TextureHandle handle{m_nextTextureIndex++, 1};
  m_textures.push_back(TextureRecord{
      .handle = handle,
      .info = info,
  });
  return handle;
}

auto RecordingRenderBackend::UpdateTexture(
    const TextureHandle texture, const TextureUpdateInfo &update) noexcept
    -> UIResult<void> {
  auto *record = FindTexture(texture);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown texture", Name(),
                       "UpdateTexture");
  }
  m_textureUpdates.push_back(CopyTextureUpdate(texture, update));
  return {};
}

auto RecordingRenderBackend::DestroyTexture(
    const TextureHandle texture) noexcept -> UIResult<void> {
  auto *record = FindTexture(texture);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::InvalidArgument, "Unknown texture", Name(),
                       "DestroyTexture");
  }
  record->destroyed = true;
  return {};
}

auto RecordingRenderBackend::Render(const RenderSurfaceHandle surface,
                                    const RenderPacket &packet) noexcept
    -> UIResult<void> {
  auto *record = FindSurface(surface);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::RenderFailed, "Unknown render surface",
                       Name(), "Render");
  }

  RecordedRenderPacket copy{
      .surface = surface,
      .vertices = std::vector<RenderVertex>{packet.vertices.begin(),
                                            packet.vertices.end()},
      .indices =
          std::vector<UInt32>{packet.indices.begin(), packet.indices.end()},
      .batches = std::vector<RenderBatch>{packet.batches.begin(),
                                          packet.batches.end()},
      .targetSize = packet.targetSize,
      .scaleFactor = packet.scaleFactor,
      .clearColor = packet.clearColor,
  };
  copy.textureUpdates.reserve(packet.textureUpdates.size());
  for (const auto &update : packet.textureUpdates) {
    copy.textureUpdates.push_back(
        CopyTextureUpdate(update.texture, update.update));
  }

  ++record->renderCount;
  m_renderPackets.push_back(std::move(copy));
  return {};
}

auto RecordingRenderBackend::Present(const RenderSurfaceHandle surface) noexcept
    -> UIResult<void> {
  auto *record = FindSurface(surface);
  if (record == nullptr || record->destroyed) {
    return MakeUIError(UIErrorCode::RenderFailed, "Unknown render surface",
                       Name(), "Present");
  }
  ++record->presentCount;
  return {};
}

auto RecordingRenderBackend::WaitIdle() noexcept -> UIResult<void> {
  ++m_waitIdleCount;
  return {};
}

auto RecordingRenderBackend::IsInitialized() const noexcept -> bool {
  return m_initialized;
}

auto RecordingRenderBackend::ValidationEnabled() const noexcept -> bool {
  return m_validationEnabled;
}

auto RecordingRenderBackend::WaitIdleCount() const noexcept -> UIntSize {
  return m_waitIdleCount;
}

auto RecordingRenderBackend::Surfaces() const noexcept
    -> const std::vector<RecordedSurface> & {
  return m_surfaces;
}

auto RecordingRenderBackend::RenderPackets() const noexcept
    -> const std::vector<RecordedRenderPacket> & {
  return m_renderPackets;
}

auto RecordingRenderBackend::TextureUpdates() const noexcept
    -> const std::vector<RecordedTextureUpdate> & {
  return m_textureUpdates;
}

auto RecordingRenderBackend::FindSurface(
    const RenderSurfaceHandle handle) noexcept -> RecordedSurface * {
  const auto found = std::find_if(m_surfaces.begin(), m_surfaces.end(),
                                  [handle](const RecordedSurface &surface) {
                                    return surface.handle == handle;
                                  });
  return found == m_surfaces.end() ? nullptr : &*found;
}

auto RecordingRenderBackend::FindTexture(const TextureHandle handle) noexcept
    -> TextureRecord * {
  const auto found = std::find_if(m_textures.begin(), m_textures.end(),
                                  [handle](const TextureRecord &texture) {
                                    return texture.handle == handle;
                                  });
  return found == m_textures.end() ? nullptr : &*found;
}
} // namespace NGIN::UI::Testing
