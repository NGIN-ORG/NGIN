#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Rendering.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <atomic>
#include <memory>
#include <span>
#include <vector>

namespace NGIN::UI {
struct ImagePixels final {
  PixelSize size{};
  std::vector<Byte> rgba{};

  [[nodiscard]] auto IsValid() const noexcept -> bool;
};

struct ImageMemorySource final {
  std::vector<Byte> encoded{};
};

struct ImageFileSource final {
  NGIN::Text::String path{};
};

struct ImageGeneratedSource final {
  PixelSize size{};
  NGIN::Utilities::Callable<Color(UInt32, UInt32)> pixel{};
};

enum class ImageLoadState : UInt8 {
  Loading,
  Ready,
  Failed,
  Cancelled,
};

class IImageDecoder {
public:
  virtual ~IImageDecoder() = default;

  [[nodiscard]] virtual auto
  Decode(std::span<const Byte> encoded,
         const std::atomic_bool &cancellationRequested) noexcept
      -> UIResult<ImagePixels> = 0;
};

class PortablePixmapImageDecoder final : public IImageDecoder {
public:
  [[nodiscard]] auto
  Decode(std::span<const Byte> encoded,
         const std::atomic_bool &cancellationRequested) noexcept
      -> UIResult<ImagePixels> override;
};

class ImageResource final {
public:
  [[nodiscard]] static auto FromPixels(ImagePixels pixels) noexcept
      -> UIResult<std::shared_ptr<ImageResource>>;
  [[nodiscard]] static auto
  DecodeMemoryAsync(ImageMemorySource source,
                    std::shared_ptr<IImageDecoder> decoder = {}) noexcept
      -> std::shared_ptr<ImageResource>;
  [[nodiscard]] static auto
  DecodeFileAsync(ImageFileSource source,
                  std::shared_ptr<IImageDecoder> decoder = {}) noexcept
      -> std::shared_ptr<ImageResource>;
  [[nodiscard]] static auto GenerateAsync(ImageGeneratedSource source) noexcept
      -> std::shared_ptr<ImageResource>;

  ImageResource(const ImageResource &) = delete;
  ImageResource(ImageResource &&) = delete;
  auto operator=(const ImageResource &) -> ImageResource & = delete;
  auto operator=(ImageResource &&) -> ImageResource & = delete;
  ~ImageResource();

  [[nodiscard]] auto State() const noexcept -> ImageLoadState;
  [[nodiscard]] auto Size() const noexcept -> PixelSize;
  [[nodiscard]] auto Revision() const noexcept -> UInt64;
  [[nodiscard]] auto Error() const noexcept -> UIError;
  [[nodiscard]] auto CopyPixels() const -> UIResult<ImagePixels>;
  void Cancel() noexcept;
  void Wait() noexcept;

private:
  struct Impl;
  using ImageWork = NGIN::Utilities::Callable<UIResult<ImagePixels>(
      const std::atomic_bool &)>;

  explicit ImageResource(std::shared_ptr<Impl> implementation) noexcept;
  [[nodiscard]] static auto Start(ImageWork work) noexcept
      -> std::shared_ptr<ImageResource>;

  std::shared_ptr<Impl> m_impl;
};

struct ResolvedImage final {
  TextureHandle texture{};
  PixelSize size{};
  ImageLoadState state{ImageLoadState::Loading};
};

class IImageResolver {
public:
  virtual ~IImageResolver() = default;

  [[nodiscard]] virtual auto
  Resolve(const std::shared_ptr<ImageResource> &resource) noexcept
      -> UIResult<ResolvedImage> = 0;
};

class ImageTextureCache final : public IImageResolver {
public:
  explicit ImageTextureCache(IRenderBackend &renderer);
  ImageTextureCache(const ImageTextureCache &) = delete;
  ImageTextureCache(ImageTextureCache &&) = delete;
  auto operator=(const ImageTextureCache &) -> ImageTextureCache & = delete;
  auto operator=(ImageTextureCache &&) -> ImageTextureCache & = delete;
  ~ImageTextureCache() override;

  [[nodiscard]] auto
  Resolve(const std::shared_ptr<ImageResource> &resource) noexcept
      -> UIResult<ResolvedImage> override;
  void Invalidate(const std::shared_ptr<ImageResource> &resource) noexcept;
  void OnDeviceLost() noexcept;
  void OnDeviceRestored(IRenderBackend &renderer) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

enum class ImageFit : UInt8 {
  None,
  Fill,
  Contain,
  Cover,
  ScaleDown,
};

struct ImageAlignment final {
  F32 horizontal{0.5F};
  F32 vertical{0.5F};
};
} // namespace NGIN::UI
