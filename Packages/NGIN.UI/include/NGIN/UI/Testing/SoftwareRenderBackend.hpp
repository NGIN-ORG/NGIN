#pragma once

#include <NGIN/UI/Rendering.hpp>

#include <memory>
#include <vector>

namespace NGIN::UI::Testing {
/// @brief One unpremultiplied RGBA8 pixel returned by the software renderer.
struct SoftwarePixel final {
  UInt8 red{0};
  UInt8 green{0};
  UInt8 blue{0};
  UInt8 alpha{0};
};

/// @brief Immutable copy of a software-rendered surface.
struct SoftwareSurfaceSnapshot final {
  PixelSize size{};
  std::vector<Byte> rgba{};
  UInt64 frameNumber{0};

  [[nodiscard]] auto Pixel(UInt32 x, UInt32 y) const noexcept -> SoftwarePixel;
};

/// @brief Per-channel and aggregate tolerances for a visual comparison.
struct VisualTolerance final {
  UInt8 channelDelta{2};
  F64 maximumDifferentPixelRatio{0.001};
  F64 maximumMeanAbsoluteError{0.5};
};

/// @brief Difference metrics produced by a tolerant visual comparison.
struct VisualComparison final {
  UIntSize differentPixelCount{0};
  UInt8 maximumChannelDelta{0};
  F64 differentPixelRatio{0.0};
  F64 meanAbsoluteError{0.0};
  bool dimensionsMatch{false};
  bool passed{false};
};

/// @brief Compares two RGBA8 snapshots using explicit pixel tolerances.
[[nodiscard]] auto CompareVisuals(const SoftwareSurfaceSnapshot &expected,
                                  const SoftwareSurfaceSnapshot &actual,
                                  VisualTolerance tolerance = {}) noexcept
    -> VisualComparison;

/// @brief Deterministic CPU triangle renderer for pixel-level tests.
class SoftwareRenderBackend final : public IRenderBackend {
public:
  SoftwareRenderBackend();
  SoftwareRenderBackend(const SoftwareRenderBackend &) = delete;
  SoftwareRenderBackend(SoftwareRenderBackend &&) = delete;
  auto operator=(const SoftwareRenderBackend &)
      -> SoftwareRenderBackend & = delete;
  auto operator=(SoftwareRenderBackend &&) -> SoftwareRenderBackend & = delete;
  ~SoftwareRenderBackend() override;

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

  [[nodiscard]] auto Snapshot(RenderSurfaceHandle surface) const noexcept
      -> UIResult<SoftwareSurfaceSnapshot>;
  [[nodiscard]] auto RenderCount() const noexcept -> UInt64;
  [[nodiscard]] auto LiveTextureCount() const noexcept -> UIntSize;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace NGIN::UI::Testing
