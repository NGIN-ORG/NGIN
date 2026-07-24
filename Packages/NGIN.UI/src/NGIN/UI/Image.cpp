#include <NGIN/UI/Image.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto ImageError(const UIErrorCode code, const char *message,
                              const char *operation) -> UIError {
  return MakeUIError(code, message, "NGIN.UI", operation, "Image");
}

[[nodiscard]] auto ByteValue(const Byte value) noexcept -> UInt8 {
  return std::to_integer<UInt8>(value);
}

[[nodiscard]] auto ReadToken(const std::span<const Byte> bytes,
                             UIntSize &offset) -> std::string_view {
  while (offset < bytes.size()) {
    const auto character = static_cast<char>(ByteValue(bytes[offset]));
    if (character == '#') {
      while (offset < bytes.size() &&
             static_cast<char>(ByteValue(bytes[offset])) != '\n') {
        ++offset;
      }
    } else if (character == ' ' || character == '\t' || character == '\r' ||
               character == '\n') {
      ++offset;
    } else {
      break;
    }
  }
  const auto start = offset;
  while (offset < bytes.size()) {
    const auto character = static_cast<char>(ByteValue(bytes[offset]));
    if (character == ' ' || character == '\t' || character == '\r' ||
        character == '\n' || character == '#') {
      break;
    }
    ++offset;
  }
  return {reinterpret_cast<const char *>(bytes.data() + start), offset - start};
}

[[nodiscard]] auto ParseUInt(const std::string_view token,
                             UInt32 &value) noexcept -> bool {
  if (token.empty()) {
    return false;
  }
  const auto result =
      std::from_chars(token.data(), token.data() + token.size(), value);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

[[nodiscard]] auto DecodePortablePixmap(const std::span<const Byte> bytes)
    -> UIResult<ImagePixels> {
  UIntSize offset = 0;
  const auto magic = ReadToken(bytes, offset);
  if (magic != "P6" && magic != "P3") {
    return ImageError(UIErrorCode::Unsupported,
                      "Only P6 and P3 portable pixmap images are supported",
                      "DecodeImage");
  }
  UInt32 width = 0;
  UInt32 height = 0;
  UInt32 maximum = 0;
  if (!ParseUInt(ReadToken(bytes, offset), width) ||
      !ParseUInt(ReadToken(bytes, offset), height) ||
      !ParseUInt(ReadToken(bytes, offset), maximum) || width == 0 ||
      height == 0 || maximum == 0 || maximum > 255) {
    return ImageError(UIErrorCode::ResourceFailed,
                      "The portable pixmap header is invalid", "DecodeImage");
  }
  const auto pixelCount = static_cast<UIntSize>(width) * height;
  if (pixelCount > std::numeric_limits<UIntSize>::max() / 4U) {
    return ImageError(UIErrorCode::OutOfMemory,
                      "The decoded image dimensions are too large",
                      "DecodeImage");
  }

  ImagePixels result{
      .size = PixelSize{width, height},
      .rgba = std::vector<Byte>(pixelCount * 4U),
  };
  if (magic == "P6") {
    if (offset >= bytes.size()) {
      return ImageError(UIErrorCode::ResourceFailed,
                        "The portable pixmap payload is missing",
                        "DecodeImage");
    }
    const auto separator = static_cast<char>(ByteValue(bytes[offset]));
    if (separator != ' ' && separator != '\t' && separator != '\r' &&
        separator != '\n') {
      return ImageError(UIErrorCode::ResourceFailed,
                        "The portable pixmap header separator is invalid",
                        "DecodeImage");
    }
    ++offset;
    if (separator == '\r' && offset < bytes.size() &&
        static_cast<char>(ByteValue(bytes[offset])) == '\n') {
      ++offset;
    }
    if (bytes.size() - offset < pixelCount * 3U) {
      return ImageError(UIErrorCode::ResourceFailed,
                        "The portable pixmap payload is truncated",
                        "DecodeImage");
    }
    for (UIntSize pixel = 0; pixel < pixelCount; ++pixel) {
      for (UIntSize channel = 0; channel < 3; ++channel) {
        const auto value = static_cast<UInt32>(
            ByteValue(bytes[offset + pixel * 3U + channel]));
        if (value > maximum) {
          return ImageError(UIErrorCode::ResourceFailed,
                            "The portable pixmap payload exceeds its range",
                            "DecodeImage");
        }
        result.rgba[pixel * 4U + channel] =
            static_cast<Byte>(value * 255U / maximum);
      }
      result.rgba[pixel * 4U + 3U] = Byte{255};
    }
  } else {
    for (UIntSize pixel = 0; pixel < pixelCount; ++pixel) {
      for (UIntSize channel = 0; channel < 3; ++channel) {
        UInt32 value = 0;
        if (!ParseUInt(ReadToken(bytes, offset), value) || value > maximum) {
          return ImageError(UIErrorCode::ResourceFailed,
                            "The portable pixmap payload is invalid",
                            "DecodeImage");
        }
        result.rgba[pixel * 4U + channel] =
            static_cast<Byte>(value * 255U / maximum);
      }
      result.rgba[pixel * 4U + 3U] = Byte{255};
    }
  }
  return result;
}

[[nodiscard]] auto ToChannel(const F32 value) noexcept -> Byte {
  return static_cast<Byte>(
      static_cast<UInt8>(std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F)));
}
} // namespace

auto PortablePixmapImageDecoder::Decode(
    const std::span<const Byte> encoded,
    const std::atomic_bool &cancellationRequested) noexcept
    -> UIResult<ImagePixels> {
  if (cancellationRequested.load()) {
    return ImageError(UIErrorCode::InvalidState, "Image decoding was cancelled",
                      "DecodeImage");
  }
  try {
    return DecodePortablePixmap(encoded);
  } catch (...) {
    return ImageError(UIErrorCode::OutOfMemory,
                      "Unable to allocate decoded image pixels", "DecodeImage");
  }
}

struct ImageResource::Impl final {
  mutable std::mutex mutex{};
  std::atomic_bool cancellationRequested{false};
  ImageLoadState state{ImageLoadState::Loading};
  ImagePixels pixels{};
  UIError error{};
  UInt64 revision{1};
  std::jthread worker{};
};

auto ImagePixels::IsValid() const noexcept -> bool {
  if (size.IsEmpty()) {
    return false;
  }
  const auto pixelCount = static_cast<UIntSize>(size.width) * size.height;
  return pixelCount <= std::numeric_limits<UIntSize>::max() / 4U &&
         rgba.size() == pixelCount * 4U;
}

ImageResource::ImageResource(std::shared_ptr<Impl> implementation) noexcept
    : m_impl(std::move(implementation)) {}

ImageResource::~ImageResource() {
  Cancel();
  Wait();
}

auto ImageResource::FromPixels(ImagePixels pixels) noexcept
    -> UIResult<std::shared_ptr<ImageResource>> {
  try {
    if (!pixels.IsValid()) {
      return ImageError(UIErrorCode::InvalidArgument,
                        "RGBA image pixels do not match their dimensions",
                        "CreateImage");
    }
    auto implementation = std::make_shared<Impl>();
    implementation->state = ImageLoadState::Ready;
    implementation->pixels = std::move(pixels);
    return std::shared_ptr<ImageResource>{
        new ImageResource{std::move(implementation)}};
  } catch (...) {
    return ImageError(UIErrorCode::OutOfMemory,
                      "Unable to allocate an image resource", "CreateImage");
  }
}

auto ImageResource::Start(ImageWork work) noexcept
    -> std::shared_ptr<ImageResource> {
  try {
    auto implementation = std::make_shared<ImageResource::Impl>();
    auto resource =
        std::shared_ptr<ImageResource>{new ImageResource{implementation}};
    implementation->worker =
        std::jthread{[implementation, work = std::move(work)]() mutable {
          auto decoded = [&]() -> UIResult<ImagePixels> {
            try {
              return work(implementation->cancellationRequested);
            } catch (const std::bad_alloc &) {
              return ImageError(UIErrorCode::OutOfMemory,
                                "Image decoding allocation failed",
                                "DecodeImageAsync");
            } catch (...) {
              return ImageError(UIErrorCode::ResourceFailed,
                                "Image decoding callback threw an exception",
                                "DecodeImageAsync");
            }
          }();
          std::scoped_lock lock{implementation->mutex};
          if (implementation->cancellationRequested.load()) {
            implementation->pixels = {};
            implementation->state = ImageLoadState::Cancelled;
          } else if (!decoded) {
            implementation->pixels = {};
            implementation->error = std::move(decoded).Error();
            implementation->state = ImageLoadState::Failed;
          } else {
            implementation->pixels = std::move(decoded).Value();
            implementation->state = ImageLoadState::Ready;
          }
          ++implementation->revision;
        }};
    return resource;
  } catch (...) {
    auto implementation = std::make_shared<ImageResource::Impl>();
    implementation->state = ImageLoadState::Failed;
    implementation->error = ImageError(
        UIErrorCode::OutOfMemory, "Unable to start asynchronous image decoding",
        "DecodeImageAsync");
    return std::shared_ptr<ImageResource>{
        new ImageResource{std::move(implementation)}};
  }
}

auto ImageResource::DecodeMemoryAsync(
    ImageMemorySource source, std::shared_ptr<IImageDecoder> decoder) noexcept
    -> std::shared_ptr<ImageResource> {
  return Start([source = std::move(source), decoder = std::move(decoder)](
                   const std::atomic_bool &cancelled) -> UIResult<ImagePixels> {
    if (decoder) {
      return decoder->Decode(source.encoded, cancelled);
    }
    PortablePixmapImageDecoder fallback;
    return fallback.Decode(source.encoded, cancelled);
  });
}

auto ImageResource::DecodeFileAsync(
    ImageFileSource source, std::shared_ptr<IImageDecoder> decoder) noexcept
    -> std::shared_ptr<ImageResource> {
  return Start([source = std::move(source), decoder = std::move(decoder)](
                   const std::atomic_bool &cancelled) -> UIResult<ImagePixels> {
    if (cancelled.load()) {
      return ImageError(UIErrorCode::InvalidState,
                        "Image decoding was cancelled", "DecodeImage");
    }
    std::ifstream stream{source.path.CStr(), std::ios::binary};
    if (!stream) {
      return ImageError(UIErrorCode::ResourceFailed,
                        "The image file could not be opened", "ReadImage");
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) {
      return ImageError(UIErrorCode::ResourceFailed,
                        "The image file length could not be read", "ReadImage");
    }
    stream.seekg(0, std::ios::beg);
    std::vector<Byte> bytes(static_cast<UIntSize>(length));
    stream.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream && !bytes.empty()) {
      return ImageError(UIErrorCode::ResourceFailed,
                        "The image file could not be read", "ReadImage");
    }
    if (cancelled.load()) {
      return ImageError(UIErrorCode::InvalidState,
                        "Image decoding was cancelled", "DecodeImage");
    }
    if (decoder) {
      return decoder->Decode(bytes, cancelled);
    }
    PortablePixmapImageDecoder fallback;
    return fallback.Decode(bytes, cancelled);
  });
}

auto ImageResource::GenerateAsync(ImageGeneratedSource source) noexcept
    -> std::shared_ptr<ImageResource> {
  return Start(
      [source = std::move(source)](
          const std::atomic_bool &cancelled) mutable -> UIResult<ImagePixels> {
        if (source.size.IsEmpty() || !source.pixel) {
          return ImageError(
              UIErrorCode::InvalidArgument,
              "Generated images require dimensions and a pixel callback",
              "GenerateImage");
        }
        const auto pixelCount =
            static_cast<UIntSize>(source.size.width) * source.size.height;
        if (pixelCount > std::numeric_limits<UIntSize>::max() / 4U) {
          return ImageError(UIErrorCode::OutOfMemory,
                            "The generated image dimensions are too large",
                            "GenerateImage");
        }
        ImagePixels pixels{
            .size = source.size,
            .rgba = std::vector<Byte>(pixelCount * 4U),
        };
        for (UInt32 y = 0; y < source.size.height; ++y) {
          if (cancelled.load()) {
            return ImageError(UIErrorCode::InvalidState,
                              "Image generation was cancelled",
                              "GenerateImage");
          }
          for (UInt32 x = 0; x < source.size.width; ++x) {
            const auto color = source.pixel(x, y);
            const auto offset =
                (static_cast<UIntSize>(y) * source.size.width + x) * 4U;
            pixels.rgba[offset] = ToChannel(color.red);
            pixels.rgba[offset + 1U] = ToChannel(color.green);
            pixels.rgba[offset + 2U] = ToChannel(color.blue);
            pixels.rgba[offset + 3U] = ToChannel(color.alpha);
          }
        }
        return pixels;
      });
}

auto ImageResource::State() const noexcept -> ImageLoadState {
  std::scoped_lock lock{m_impl->mutex};
  return m_impl->state;
}

auto ImageResource::Size() const noexcept -> PixelSize {
  std::scoped_lock lock{m_impl->mutex};
  return m_impl->pixels.size;
}

auto ImageResource::Revision() const noexcept -> UInt64 {
  std::scoped_lock lock{m_impl->mutex};
  return m_impl->revision;
}

auto ImageResource::Error() const noexcept -> UIError {
  std::scoped_lock lock{m_impl->mutex};
  return m_impl->error;
}

auto ImageResource::CopyPixels() const -> UIResult<ImagePixels> {
  std::scoped_lock lock{m_impl->mutex};
  if (m_impl->state != ImageLoadState::Ready) {
    return m_impl->state == ImageLoadState::Failed
               ? m_impl->error
               : ImageError(UIErrorCode::InvalidState,
                            "Image pixels are not ready", "ReadImagePixels");
  }
  return m_impl->pixels;
}

void ImageResource::Cancel() noexcept {
  m_impl->cancellationRequested.store(true);
}

void ImageResource::Wait() noexcept {
  if (m_impl->worker.joinable() &&
      m_impl->worker.get_id() != std::this_thread::get_id()) {
    m_impl->worker.join();
  }
}

struct ImageTextureCache::Impl final {
  struct Entry final {
    std::weak_ptr<ImageResource> resource{};
    TextureHandle texture{};
    PixelSize size{};
    UInt64 revision{0};
  };

  IRenderBackend *renderer{nullptr};
  std::unordered_map<const ImageResource *, Entry> entries{};
  ImageCacheDiagnostics diagnostics{};

  void DestroyAll() noexcept {
    if (renderer != nullptr) {
      for (auto &[_, entry] : entries) {
        if (entry.texture) {
          static_cast<void>(renderer->DestroyTexture(entry.texture));
        }
      }
    }
    diagnostics.evictionCount += static_cast<UInt64>(entries.size());
    entries.clear();
    diagnostics.entryCount = 0;
  }
};

ImageTextureCache::ImageTextureCache(IRenderBackend &renderer)
    : m_impl(std::make_unique<Impl>()) {
  m_impl->renderer = &renderer;
}

ImageTextureCache::~ImageTextureCache() { m_impl->DestroyAll(); }

auto ImageTextureCache::Resolve(
    const std::shared_ptr<ImageResource> &resource) noexcept
    -> UIResult<ResolvedImage> {
  if (!resource) {
    return ImageError(UIErrorCode::InvalidArgument,
                      "Image resolution requires a logical resource",
                      "ResolveImage");
  }
  const auto state = resource->State();
  if (state != ImageLoadState::Ready) {
    if (state == ImageLoadState::Failed) {
      return resource->Error();
    }
    return ResolvedImage{.state = state};
  }
  if (m_impl->renderer == nullptr) {
    return ImageError(UIErrorCode::InvalidState,
                      "Image textures are unavailable while the device is lost",
                      "ResolveImage");
  }

  const auto revision = resource->Revision();
  auto found = m_impl->entries.find(resource.get());
  if (found != m_impl->entries.end() &&
      found->second.resource.lock() != resource) {
    if (found->second.texture) {
      static_cast<void>(
          m_impl->renderer->DestroyTexture(found->second.texture));
    }
    ++m_impl->diagnostics.evictionCount;
    m_impl->entries.erase(found);
    found = m_impl->entries.end();
  }
  if (found != m_impl->entries.end() && found->second.revision == revision &&
      found->second.texture) {
    ++m_impl->diagnostics.hitCount;
    return ResolvedImage{
        .texture = found->second.texture,
        .size = found->second.size,
        .state = ImageLoadState::Ready,
    };
  }
  if (found != m_impl->entries.end() && found->second.texture) {
    static_cast<void>(m_impl->renderer->DestroyTexture(found->second.texture));
    ++m_impl->diagnostics.evictionCount;
    m_impl->entries.erase(found);
  }

  ++m_impl->diagnostics.missCount;
  auto pixels = resource->CopyPixels();
  if (!pixels) {
    return std::move(pixels).Error();
  }
  auto texture = m_impl->renderer->CreateTexture(TextureCreateInfo{
      .size = pixels.Value().size,
      .format = TextureFormat::RGBA8,
  });
  if (!texture) {
    return std::move(texture).Error();
  }
  auto updated = m_impl->renderer->UpdateTexture(
      texture.Value(),
      TextureUpdateInfo{
          .region = PixelRect{0, 0, pixels.Value().size.width,
                              pixels.Value().size.height},
          .bytesPerRow = static_cast<UIntSize>(pixels.Value().size.width) * 4U,
          .bytes = pixels.Value().rgba,
      });
  if (!updated) {
    static_cast<void>(m_impl->renderer->DestroyTexture(texture.Value()));
    return std::move(updated).Error();
  }
  const auto size = pixels.Value().size;
  m_impl->entries[resource.get()] = Impl::Entry{
      .resource = resource,
      .texture = texture.Value(),
      .size = size,
      .revision = revision,
  };
  ++m_impl->diagnostics.uploadCount;
  m_impl->diagnostics.entryCount = m_impl->entries.size();
  return ResolvedImage{
      .texture = texture.Value(),
      .size = size,
      .state = ImageLoadState::Ready,
  };
}

void ImageTextureCache::Invalidate(
    const std::shared_ptr<ImageResource> &resource) noexcept {
  if (!resource) {
    return;
  }
  const auto found = m_impl->entries.find(resource.get());
  if (found == m_impl->entries.end()) {
    return;
  }
  if (m_impl->renderer != nullptr && found->second.texture) {
    static_cast<void>(m_impl->renderer->DestroyTexture(found->second.texture));
  }
  ++m_impl->diagnostics.evictionCount;
  m_impl->entries.erase(found);
  m_impl->diagnostics.entryCount = m_impl->entries.size();
}

void ImageTextureCache::OnDeviceLost() noexcept {
  m_impl->DestroyAll();
  m_impl->renderer = nullptr;
}

void ImageTextureCache::OnDeviceRestored(IRenderBackend &renderer) noexcept {
  m_impl->DestroyAll();
  m_impl->renderer = &renderer;
}

auto ImageTextureCache::Diagnostics() const noexcept -> ImageCacheDiagnostics {
  auto diagnostics = m_impl->diagnostics;
  diagnostics.entryCount = m_impl->entries.size();
  return diagnostics;
}
} // namespace NGIN::UI
