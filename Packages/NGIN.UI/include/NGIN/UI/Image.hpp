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
/// @brief Decoded tightly packed RGBA8 pixels and their dimensions.
struct ImagePixels final {
  PixelSize size{};
  std::vector<Byte> rgba{};

  [[nodiscard]] auto IsValid() const noexcept -> bool;
};

/// @brief Encoded image bytes supplied directly by the application.
struct ImageMemorySource final {
  std::vector<Byte> encoded{};
};

/// @brief File-system path from which encoded image data is loaded.
struct ImageFileSource final {
  NGIN::Text::String path{};
};

/// @brief Callback that generates decoded image pixels on demand.
struct ImageGeneratedSource final {
  PixelSize size{};
  NGIN::Utilities::Callable<Color(UInt32, UInt32)> pixel{};
};

/// @brief Lifecycle state of a logical image resource.
enum class ImageLoadState : UInt8 {
  Loading,
  Ready,
  Failed,
  Cancelled,
};

/// @brief Extension interface that decodes encoded bytes into RGBA8 pixels.
class IImageDecoder {
public:
  virtual ~IImageDecoder() = default;

  [[nodiscard]] virtual auto
  Decode(std::span<const Byte> encoded,
         const std::atomic_bool &cancellationRequested) noexcept
      -> UIResult<ImagePixels> = 0;
};

/// @brief Built-in decoder for binary and ASCII portable pixmap images.
class PortablePixmapImageDecoder final : public IImageDecoder {
public:
  [[nodiscard]] auto
  Decode(std::span<const Byte> encoded,
         const std::atomic_bool &cancellationRequested) noexcept
      -> UIResult<ImagePixels> override;
};

/// @brief Built-in decoder for PNG, JPEG, and portable pixmap images.
///
/// PNG is decoded to straight RGBA8, JPEG to opaque RGBA8, and P3/P6 PPM to
/// opaque RGBA8. Animated images and embedded color-profile conversion are not
/// part of this decoder.
class StandardImageDecoder final : public IImageDecoder {
public:
  [[nodiscard]] auto
  Decode(std::span<const Byte> encoded,
         const std::atomic_bool &cancellationRequested) noexcept
      -> UIResult<ImagePixels> override;
};

/// @brief Logical, shareable image source with lazy loading and stable identity.
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

/// @brief Renderer texture and pixel dimensions resolved for an image resource.
struct ResolvedImage final {
  TextureHandle texture{};
  PixelSize size{};
  ImageLoadState state{ImageLoadState::Loading};
};

/// @brief Hit, miss, upload, eviction, and occupancy counters for image textures.
struct ImageCacheDiagnostics final {
  UInt64 hitCount{0};
  UInt64 missCount{0};
  UInt64 uploadCount{0};
  UInt64 evictionCount{0};
  UInt64 capacityFailureCount{0};
  UIntSize entryCount{0};
  UIntSize maximumEntryCount{0};
  UIntSize peakEntryCount{0};
  UInt64 residentBytes{0};
  UInt64 maximumResidentBytes{0};
  UInt64 peakResidentBytes{0};
};

/// @brief Fixed texture-entry and RGBA8 storage budgets for an image cache.
struct ImageTextureCacheOptions final {
  UIntSize maximumEntries{128};
  UInt64 maximumResidentBytes{256ULL * 1024ULL * 1024ULL};
};

/// @brief Resolves logical image resources to renderer-owned textures.
class IImageResolver {
public:
  virtual ~IImageResolver() = default;

  [[nodiscard]] virtual auto
  Resolve(const std::shared_ptr<ImageResource> &resource) noexcept
      -> UIResult<ResolvedImage> = 0;
};

/// @brief Lazily decodes, uploads, caches, and releases image textures.
class ImageTextureCache final : public IImageResolver {
public:
  explicit ImageTextureCache(IRenderBackend &renderer,
                             ImageTextureCacheOptions options = {});
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
  [[nodiscard]] auto Diagnostics() const noexcept -> ImageCacheDiagnostics;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

/// @brief Policy for fitting an image into its arranged content rectangle.
enum class ImageFit : UInt8 {
  None,
  Fill,
  Contain,
  Cover,
  ScaleDown,
};

/// @brief Normalized horizontal and vertical placement of a fitted image.
struct ImageAlignment final {
  F32 horizontal{0.5F};
  F32 vertical{0.5F};
};
} // namespace NGIN::UI
