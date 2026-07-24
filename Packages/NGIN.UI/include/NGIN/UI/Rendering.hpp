#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Backend.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/Style.hpp>

#include <span>

namespace NGIN::UI {
enum class TextureFormat : UInt8 {
  R8,
  RGBA8,
  BGRA8,
};

enum class BlendMode : UInt8 {
  Opaque,
  PremultipliedAlpha,
};

struct RenderInitInfo final {
  bool enableValidation{false};
};

struct TextureCreateInfo final {
  PixelSize size{};
  TextureFormat format{TextureFormat::RGBA8};
};

struct TextureUpdateInfo final {
  PixelRect region{};
  UIntSize bytesPerRow{0};
  std::span<const Byte> bytes{};
};

struct TextureUpdate final {
  TextureHandle texture{};
  TextureUpdateInfo update{};
};

struct RenderVertex final {
  F32 x{0.0F};
  F32 y{0.0F};
  F32 u{0.0F};
  F32 v{0.0F};
  UInt32 color{0};
};

struct RenderBatch final {
  TextureHandle texture{};
  PixelRect scissor{};
  UInt32 firstIndex{0};
  UInt32 indexCount{0};
  BlendMode blendMode{BlendMode::PremultipliedAlpha};
};

struct RenderPacket final {
  std::span<const RenderVertex> vertices{};
  std::span<const UInt32> indices{};
  std::span<const RenderBatch> batches{};
  std::span<const TextureUpdate> textureUpdates{};
  PixelSize targetSize{};
  F32 scaleFactor{1.0F};
  Color clearColor{0.08F, 0.09F, 0.11F, 1.0F};
};

class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;

  [[nodiscard]] virtual auto Name() const noexcept -> const char * = 0;
  [[nodiscard]] virtual auto ContractVersion() const noexcept
      -> BackendContractVersion = 0;
  [[nodiscard]] virtual auto Capabilities() const noexcept
      -> RenderCapabilityFlags = 0;
  virtual auto Initialize(const RenderInitInfo &info) noexcept
      -> UIResult<void> = 0;
  virtual auto CreateSurface(PlatformWindowHandle window,
                             PixelSize initialSize) noexcept
      -> UIResult<RenderSurfaceHandle> = 0;
  virtual auto DestroySurface(RenderSurfaceHandle surface) noexcept
      -> UIResult<void> = 0;
  virtual auto ResizeSurface(RenderSurfaceHandle surface,
                             PixelSize size) noexcept -> UIResult<void> = 0;
  virtual auto CreateTexture(const TextureCreateInfo &info) noexcept
      -> UIResult<TextureHandle> = 0;
  virtual auto UpdateTexture(TextureHandle texture,
                             const TextureUpdateInfo &update) noexcept
      -> UIResult<void> = 0;
  virtual auto DestroyTexture(TextureHandle texture) noexcept
      -> UIResult<void> = 0;
  virtual auto Render(RenderSurfaceHandle surface,
                      const RenderPacket &packet) noexcept
      -> UIResult<void> = 0;
  virtual auto Present(RenderSurfaceHandle surface) noexcept
      -> UIResult<void> = 0;
  virtual auto WaitIdle() noexcept -> UIResult<void> = 0;
};
} // namespace NGIN::UI
