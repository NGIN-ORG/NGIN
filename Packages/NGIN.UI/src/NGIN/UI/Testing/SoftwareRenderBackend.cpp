#include <NGIN/UI/Testing/SoftwareRenderBackend.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_map>

namespace NGIN::UI::Testing {
namespace {
[[nodiscard]] auto SoftwareError(const UIErrorCode code, const char *message,
                                 const char *operation) -> UIError {
  return MakeUIError(code, message, "SoftwareRenderer", operation);
}

[[nodiscard]] constexpr auto BytesPerPixel(const TextureFormat format) noexcept
    -> UIntSize {
  return format == TextureFormat::R8 ? 1 : 4;
}

[[nodiscard]] auto CheckedByteCount(const PixelSize size,
                                    const UIntSize channels) noexcept
    -> UIResult<UIntSize> {
  const auto width = static_cast<UIntSize>(size.width);
  const auto height = static_cast<UIntSize>(size.height);
  if (size.IsEmpty() ||
      width > std::numeric_limits<UIntSize>::max() / height ||
      width * height > std::numeric_limits<UIntSize>::max() / channels) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Pixel storage dimensions overflow", "Allocate");
  }
  return width * height * channels;
}

[[nodiscard]] auto ToByte(const F32 value) noexcept -> UInt8 {
  return static_cast<UInt8>(
      std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

[[nodiscard]] auto Channel(const UInt32 color, const UInt32 shift) noexcept
    -> F32 {
  return static_cast<F32>((color >> shift) & 0xFFU) / 255.0F;
}

struct Sample final {
  F32 red{1.0F};
  F32 green{1.0F};
  F32 blue{1.0F};
  F32 alpha{1.0F};
};

[[nodiscard]] auto Lerp(const Sample first, const Sample second,
                        const F32 amount) noexcept -> Sample {
  const auto channel = [amount](const F32 left, const F32 right) {
    return left + (right - left) * amount;
  };
  return Sample{
      channel(first.red, second.red),
      channel(first.green, second.green),
      channel(first.blue, second.blue),
      channel(first.alpha, second.alpha),
  };
}

[[nodiscard]] auto Edge(const RenderVertex &first, const RenderVertex &second,
                        const F32 x, const F32 y) noexcept -> F32 {
  return (x - first.x) * (second.y - first.y) -
         (y - first.y) * (second.x - first.x);
}

[[nodiscard]] auto IsTopLeft(const RenderVertex &first,
                             const RenderVertex &second) noexcept -> bool {
  const auto deltaY = second.y - first.y;
  const auto deltaX = second.x - first.x;
  return deltaY < 0.0F || (deltaY == 0.0F && deltaX > 0.0F);
}

[[nodiscard]] auto InsideEdge(const F32 value, const bool topLeft) noexcept
    -> bool {
  constexpr F32 epsilon = 0.00001F;
  return value > epsilon || (std::abs(value) <= epsilon && topLeft);
}

[[nodiscard]] auto UnpackClear(const Color color) noexcept
    -> std::array<Byte, 4> {
  return {
      static_cast<Byte>(ToByte(color.red)),
      static_cast<Byte>(ToByte(color.green)),
      static_cast<Byte>(ToByte(color.blue)),
      static_cast<Byte>(ToByte(color.alpha)),
  };
}
} // namespace

struct SoftwareRenderBackend::Impl final {
  struct Surface final {
    PlatformWindowHandle window{};
    PixelSize size{};
    std::vector<Byte> rgba{};
    UInt64 frameNumber{0};
    bool live{true};
  };

  struct Texture final {
    TextureCreateInfo info{};
    std::vector<Byte> bytes{};
    bool live{true};
  };

  bool initialized{false};
  UInt32 nextSurface{0};
  UInt32 nextTexture{0};
  UInt64 renderCount{0};
  std::unordered_map<UInt32, Surface> surfaces{};
  std::unordered_map<UInt32, Texture> textures{};

  [[nodiscard]] auto FindSurface(const RenderSurfaceHandle handle) noexcept
      -> Surface * {
    const auto found = surfaces.find(handle.index);
    return handle.generation == 1 && found != surfaces.end() &&
                   found->second.live
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] auto
  FindSurface(const RenderSurfaceHandle handle) const noexcept
      -> const Surface * {
    const auto found = surfaces.find(handle.index);
    return handle.generation == 1 && found != surfaces.end() &&
                   found->second.live
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] auto FindTexture(const TextureHandle handle) noexcept
      -> Texture * {
    const auto found = textures.find(handle.index);
    return handle.generation == 1 && found != textures.end() &&
                   found->second.live
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] auto FindTexture(const TextureHandle handle) const noexcept
      -> const Texture * {
    const auto found = textures.find(handle.index);
    return handle.generation == 1 && found != textures.end() &&
                   found->second.live
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] auto SampleTexture(const TextureHandle handle, const F32 u,
                                   const F32 v) const noexcept -> Sample {
    const auto *texture = FindTexture(handle);
    if (texture == nullptr) {
      return {};
    }
    const auto sampleAt = [texture](const Int32 sourceX,
                                    const Int32 sourceY) noexcept {
      const auto x = static_cast<UInt32>(std::clamp(
          sourceX, 0, static_cast<Int32>(texture->info.size.width) - 1));
      const auto y = static_cast<UInt32>(std::clamp(
          sourceY, 0, static_cast<Int32>(texture->info.size.height) - 1));
      const auto offset =
          static_cast<UIntSize>(y * texture->info.size.width + x) *
          BytesPerPixel(texture->info.format);
      if (texture->info.format == TextureFormat::R8) {
        const auto alpha = static_cast<F32>(
                               static_cast<UInt8>(texture->bytes[offset])) /
                           255.0F;
        return Sample{alpha, alpha, alpha, alpha};
      }
      const auto first =
          static_cast<F32>(static_cast<UInt8>(texture->bytes[offset])) /
          255.0F;
      const auto second =
          static_cast<F32>(static_cast<UInt8>(texture->bytes[offset + 1])) /
          255.0F;
      const auto third =
          static_cast<F32>(static_cast<UInt8>(texture->bytes[offset + 2])) /
          255.0F;
      const auto alpha =
          static_cast<F32>(static_cast<UInt8>(texture->bytes[offset + 3])) /
          255.0F;
      return texture->info.format == TextureFormat::BGRA8
                 ? Sample{third, second, first, alpha}
                 : Sample{first, second, third, alpha};
    };

    const auto clampedU = std::clamp(u, 0.0F, 1.0F);
    const auto clampedV = std::clamp(v, 0.0F, 1.0F);
    if (texture->info.filter == TextureFilter::Nearest) {
      return sampleAt(
          static_cast<Int32>(std::floor(
              clampedU * static_cast<F32>(texture->info.size.width))),
          static_cast<Int32>(std::floor(
              clampedV * static_cast<F32>(texture->info.size.height))));
    }

    const auto sourceX =
        clampedU * static_cast<F32>(texture->info.size.width) - 0.5F;
    const auto sourceY =
        clampedV * static_cast<F32>(texture->info.size.height) - 0.5F;
    const auto left = static_cast<Int32>(std::floor(sourceX));
    const auto top = static_cast<Int32>(std::floor(sourceY));
    const auto horizontal = sourceX - static_cast<F32>(left);
    const auto vertical = sourceY - static_cast<F32>(top);
    return Lerp(Lerp(sampleAt(left, top), sampleAt(left + 1, top), horizontal),
                Lerp(sampleAt(left, top + 1), sampleAt(left + 1, top + 1),
                     horizontal),
                vertical);
  }

  void BlendPixel(Surface &surface, const UInt32 x, const UInt32 y,
                  const Sample source, const BlendMode blendMode) noexcept {
    const auto offset =
        static_cast<UIntSize>(y * surface.size.width + x) * 4U;
    if (blendMode == BlendMode::Opaque) {
      surface.rgba[offset] = static_cast<Byte>(ToByte(source.red));
      surface.rgba[offset + 1] = static_cast<Byte>(ToByte(source.green));
      surface.rgba[offset + 2] = static_cast<Byte>(ToByte(source.blue));
      surface.rgba[offset + 3] = static_cast<Byte>(ToByte(source.alpha));
      return;
    }
    const auto inverseAlpha = 1.0F - source.alpha;
    const auto destination = Sample{
        static_cast<F32>(static_cast<UInt8>(surface.rgba[offset])) / 255.0F,
        static_cast<F32>(static_cast<UInt8>(surface.rgba[offset + 1])) /
            255.0F,
        static_cast<F32>(static_cast<UInt8>(surface.rgba[offset + 2])) /
            255.0F,
        static_cast<F32>(static_cast<UInt8>(surface.rgba[offset + 3])) /
            255.0F,
    };
    surface.rgba[offset] =
        static_cast<Byte>(ToByte(source.red + destination.red * inverseAlpha));
    surface.rgba[offset + 1] = static_cast<Byte>(
        ToByte(source.green + destination.green * inverseAlpha));
    surface.rgba[offset + 2] =
        static_cast<Byte>(ToByte(source.blue + destination.blue * inverseAlpha));
    surface.rgba[offset + 3] = static_cast<Byte>(
        ToByte(source.alpha + destination.alpha * inverseAlpha));
  }

  auto RasterizeTriangle(Surface &surface, const RenderBatch &batch,
                         const RenderVertex &first,
                         const RenderVertex &second,
                         const RenderVertex &third) noexcept -> UIResult<void> {
    const auto area = Edge(first, second, third.x, third.y);
    if (std::abs(area) <= 0.00001F) {
      return {};
    }
    if (batch.texture && FindTexture(batch.texture) == nullptr) {
      return SoftwareError(UIErrorCode::RenderFailed,
                           "Render batch references an unknown texture",
                           "Render");
    }

    const auto scissorLeft = std::max(0, batch.scissor.x);
    const auto scissorTop = std::max(0, batch.scissor.y);
    const auto scissorRight = std::min(
        static_cast<Int32>(surface.size.width),
        batch.scissor.x + static_cast<Int32>(batch.scissor.width));
    const auto scissorBottom = std::min(
        static_cast<Int32>(surface.size.height),
        batch.scissor.y + static_cast<Int32>(batch.scissor.height));
    const auto left = std::max(
        scissorLeft, static_cast<Int32>(std::floor(
                          std::min({first.x, second.x, third.x}))));
    const auto top = std::max(
        scissorTop, static_cast<Int32>(std::floor(
                         std::min({first.y, second.y, third.y}))));
    const auto right = std::min(
        scissorRight, static_cast<Int32>(std::ceil(
                           std::max({first.x, second.x, third.x}))));
    const auto bottom = std::min(
        scissorBottom, static_cast<Int32>(std::ceil(
                            std::max({first.y, second.y, third.y}))));
    const auto sign = area < 0.0F ? -1.0F : 1.0F;
    const auto absoluteArea = std::abs(area);
    const auto firstEdgeTopLeft =
        sign > 0.0F ? IsTopLeft(second, third) : IsTopLeft(third, second);
    const auto secondEdgeTopLeft =
        sign > 0.0F ? IsTopLeft(third, first) : IsTopLeft(first, third);
    const auto thirdEdgeTopLeft =
        sign > 0.0F ? IsTopLeft(first, second) : IsTopLeft(second, first);

    for (auto y = top; y < bottom; ++y) {
      for (auto x = left; x < right; ++x) {
        const auto sampleX = static_cast<F32>(x) + 0.5F;
        const auto sampleY = static_cast<F32>(y) + 0.5F;
        const auto firstWeight =
            Edge(second, third, sampleX, sampleY) * sign;
        const auto secondWeight =
            Edge(third, first, sampleX, sampleY) * sign;
        const auto thirdWeight =
            Edge(first, second, sampleX, sampleY) * sign;
        if (!InsideEdge(firstWeight, firstEdgeTopLeft) ||
            !InsideEdge(secondWeight, secondEdgeTopLeft) ||
            !InsideEdge(thirdWeight, thirdEdgeTopLeft)) {
          continue;
        }
        const auto firstFactor = firstWeight / absoluteArea;
        const auto secondFactor = secondWeight / absoluteArea;
        const auto thirdFactor = thirdWeight / absoluteArea;
        const auto interpolate = [&](const F32 a, const F32 b,
                                     const F32 c) noexcept {
          return a * firstFactor + b * secondFactor + c * thirdFactor;
        };
        Sample source{
            interpolate(Channel(first.color, 0), Channel(second.color, 0),
                        Channel(third.color, 0)),
            interpolate(Channel(first.color, 8), Channel(second.color, 8),
                        Channel(third.color, 8)),
            interpolate(Channel(first.color, 16), Channel(second.color, 16),
                        Channel(third.color, 16)),
            interpolate(Channel(first.color, 24), Channel(second.color, 24),
                        Channel(third.color, 24)),
        };
        if (batch.texture) {
          const auto texture = SampleTexture(
              batch.texture,
              interpolate(first.u, second.u, third.u),
              interpolate(first.v, second.v, third.v));
          source.red *= texture.red;
          source.green *= texture.green;
          source.blue *= texture.blue;
          source.alpha *= texture.alpha;
        }
        BlendPixel(surface, static_cast<UInt32>(x), static_cast<UInt32>(y),
                   source, batch.blendMode);
      }
    }
    return {};
  }
};

auto SoftwareSurfaceSnapshot::Pixel(const UInt32 x,
                                    const UInt32 y) const noexcept
    -> SoftwarePixel {
  if (x >= size.width || y >= size.height ||
      rgba.size() < static_cast<UIntSize>(size.width) * size.height * 4U) {
    return {};
  }
  const auto offset = static_cast<UIntSize>(y * size.width + x) * 4U;
  return SoftwarePixel{
      static_cast<UInt8>(rgba[offset]),
      static_cast<UInt8>(rgba[offset + 1]),
      static_cast<UInt8>(rgba[offset + 2]),
      static_cast<UInt8>(rgba[offset + 3]),
  };
}

auto CompareVisuals(const SoftwareSurfaceSnapshot &expected,
                    const SoftwareSurfaceSnapshot &actual,
                    const VisualTolerance tolerance) noexcept
    -> VisualComparison {
  VisualComparison result{};
  result.dimensionsMatch =
      expected.size == actual.size && expected.rgba.size() == actual.rgba.size();
  if (!result.dimensionsMatch || expected.rgba.empty()) {
    return result;
  }

  UInt64 absoluteError = 0;
  const auto pixelCount =
      static_cast<UIntSize>(expected.size.width) * expected.size.height;
  for (UIntSize pixel = 0; pixel < pixelCount; ++pixel) {
    bool different = false;
    for (UIntSize channel = 0; channel < 4; ++channel) {
      const auto offset = pixel * 4U + channel;
      const auto left =
          static_cast<Int32>(static_cast<UInt8>(expected.rgba[offset]));
      const auto right =
          static_cast<Int32>(static_cast<UInt8>(actual.rgba[offset]));
      const auto delta = static_cast<UInt8>(std::abs(left - right));
      absoluteError += delta;
      result.maximumChannelDelta =
          std::max(result.maximumChannelDelta, delta);
      different = different || delta > tolerance.channelDelta;
    }
    if (different) {
      ++result.differentPixelCount;
    }
  }
  result.differentPixelRatio =
      static_cast<F64>(result.differentPixelCount) /
      static_cast<F64>(pixelCount);
  result.meanAbsoluteError =
      static_cast<F64>(absoluteError) / static_cast<F64>(pixelCount * 4U);
  result.passed =
      result.differentPixelRatio <= tolerance.maximumDifferentPixelRatio &&
      result.meanAbsoluteError <= tolerance.maximumMeanAbsoluteError;
  return result;
}

SoftwareRenderBackend::SoftwareRenderBackend()
    : m_impl(std::make_unique<Impl>()) {}
SoftwareRenderBackend::~SoftwareRenderBackend() = default;

auto SoftwareRenderBackend::Name() const noexcept -> const char * {
  return "SoftwareRenderer";
}
auto SoftwareRenderBackend::ContractVersion() const noexcept
    -> BackendContractVersion {
  return CurrentBackendContractVersion;
}
auto SoftwareRenderBackend::Capabilities() const noexcept
    -> RenderCapabilityFlags {
  return RenderCapabilityFlags::TextureUpdates |
         RenderCapabilityFlags::ScissorRects | RenderCapabilityFlags::Index32;
}
auto SoftwareRenderBackend::Initialize(const RenderInitInfo &) noexcept
    -> UIResult<void> {
  m_impl->initialized = true;
  return {};
}

auto SoftwareRenderBackend::CreateSurface(
    const PlatformWindowHandle window,
    const PixelSize initialSize) noexcept -> UIResult<RenderSurfaceHandle> {
  if (!m_impl->initialized || !window) {
    return SoftwareError(UIErrorCode::BackendUnavailable,
                         "Renderer initialization and a live window are required",
                         "CreateSurface");
  }
  auto byteCount = CheckedByteCount(initialSize, 4);
  if (!byteCount) {
    return byteCount.Error();
  }
  try {
    const RenderSurfaceHandle handle{m_impl->nextSurface++, 1};
    m_impl->surfaces.emplace(
        handle.index,
        Impl::Surface{.window = window,
                      .size = initialSize,
                      .rgba = std::vector<Byte>(byteCount.Value())});
    return handle;
  } catch (...) {
    return SoftwareError(UIErrorCode::OutOfMemory,
                         "Software surface allocation failed", "CreateSurface");
  }
}

auto SoftwareRenderBackend::DestroySurface(
    const RenderSurfaceHandle surface) noexcept -> UIResult<void> {
  auto *record = m_impl->FindSurface(surface);
  if (record == nullptr) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Unknown software surface", "DestroySurface");
  }
  record->live = false;
  record->rgba.clear();
  return {};
}

auto SoftwareRenderBackend::ResizeSurface(const RenderSurfaceHandle surface,
                                          const PixelSize size) noexcept
    -> UIResult<void> {
  auto *record = m_impl->FindSurface(surface);
  auto byteCount = CheckedByteCount(size, 4);
  if (record == nullptr || !byteCount) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "A live surface and valid size are required",
                         "ResizeSurface");
  }
  try {
    record->size = size;
    record->rgba.assign(byteCount.Value(), Byte{});
    return {};
  } catch (...) {
    return SoftwareError(UIErrorCode::OutOfMemory,
                         "Software surface resize failed", "ResizeSurface");
  }
}

auto SoftwareRenderBackend::CreateTexture(
    const TextureCreateInfo &info) noexcept -> UIResult<TextureHandle> {
  if (!m_impl->initialized) {
    return SoftwareError(UIErrorCode::BackendUnavailable,
                         "Renderer is not initialized", "CreateTexture");
  }
  auto byteCount = CheckedByteCount(info.size, BytesPerPixel(info.format));
  if (!byteCount) {
    return byteCount.Error();
  }
  try {
    const TextureHandle handle{m_impl->nextTexture++, 1};
    m_impl->textures.emplace(
        handle.index,
        Impl::Texture{.info = info,
                      .bytes = std::vector<Byte>(byteCount.Value())});
    return handle;
  } catch (...) {
    return SoftwareError(UIErrorCode::OutOfMemory,
                         "Software texture allocation failed", "CreateTexture");
  }
}

auto SoftwareRenderBackend::UpdateTexture(
    const TextureHandle texture, const TextureUpdateInfo &update) noexcept
    -> UIResult<void> {
  auto *record = m_impl->FindTexture(texture);
  if (record == nullptr || update.region.x < 0 || update.region.y < 0 ||
      update.region.width == 0 || update.region.height == 0) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Unknown texture or invalid update region",
                         "UpdateTexture");
  }
  const auto x = static_cast<UInt32>(update.region.x);
  const auto y = static_cast<UInt32>(update.region.y);
  if (x > record->info.size.width ||
      update.region.width > record->info.size.width - x ||
      y > record->info.size.height ||
      update.region.height > record->info.size.height - y) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Texture update exceeds its allocation",
                         "UpdateTexture");
  }
  const auto channels = BytesPerPixel(record->info.format);
  const auto rowBytes =
      static_cast<UIntSize>(update.region.width) * channels;
  const auto required =
      update.bytesPerRow * static_cast<UIntSize>(update.region.height - 1) +
      rowBytes;
  if (update.bytesPerRow < rowBytes || update.bytes.size() < required) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Texture update byte span is too small",
                         "UpdateTexture");
  }
  for (UInt32 row = 0; row < update.region.height; ++row) {
    const auto sourceOffset = static_cast<UIntSize>(row) * update.bytesPerRow;
    const auto destinationOffset =
        (static_cast<UIntSize>(y + row) * record->info.size.width + x) *
        channels;
    std::copy_n(
        update.bytes.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
        static_cast<std::ptrdiff_t>(rowBytes),
        record->bytes.begin() +
            static_cast<std::ptrdiff_t>(destinationOffset));
  }
  return {};
}

auto SoftwareRenderBackend::DestroyTexture(
    const TextureHandle texture) noexcept -> UIResult<void> {
  auto *record = m_impl->FindTexture(texture);
  if (record == nullptr) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Unknown software texture", "DestroyTexture");
  }
  record->live = false;
  record->bytes.clear();
  return {};
}

auto SoftwareRenderBackend::Render(const RenderSurfaceHandle surface,
                                   const RenderPacket &packet) noexcept
    -> UIResult<void> {
  auto *record = m_impl->FindSurface(surface);
  if (record == nullptr || packet.targetSize != record->size) {
    return SoftwareError(UIErrorCode::RenderFailed,
                         "A live surface matching the packet target is required",
                         "Render");
  }
  try {
    for (const auto &update : packet.textureUpdates) {
      auto updated = UpdateTexture(update.texture, update.update);
      if (!updated) {
        return updated.Error();
      }
    }
    const auto clear = UnpackClear(packet.clearColor);
    for (UIntSize offset = 0; offset < record->rgba.size(); offset += 4U) {
      std::copy(clear.begin(), clear.end(),
                record->rgba.begin() +
                    static_cast<std::ptrdiff_t>(offset));
    }
    for (const auto &batch : packet.batches) {
      const auto firstIndex = static_cast<UIntSize>(batch.firstIndex);
      const auto indexCount = static_cast<UIntSize>(batch.indexCount);
      if (firstIndex > packet.indices.size() ||
          indexCount > packet.indices.size() - firstIndex ||
          batch.indexCount % 3U != 0U) {
        return SoftwareError(UIErrorCode::RenderFailed,
                             "Render batch index range is invalid", "Render");
      }
      for (UIntSize index = firstIndex; index < firstIndex + indexCount;
           index += 3U) {
        const auto firstVertex = packet.indices[index];
        const auto secondVertex = packet.indices[index + 1];
        const auto thirdVertex = packet.indices[index + 2];
        if (firstVertex >= packet.vertices.size() ||
            secondVertex >= packet.vertices.size() ||
            thirdVertex >= packet.vertices.size()) {
          return SoftwareError(UIErrorCode::RenderFailed,
                               "Render batch references an invalid vertex",
                               "Render");
        }
        auto rasterized = m_impl->RasterizeTriangle(
            *record, batch, packet.vertices[firstVertex],
            packet.vertices[secondVertex], packet.vertices[thirdVertex]);
        if (!rasterized) {
          return rasterized.Error();
        }
      }
    }
    ++record->frameNumber;
    ++m_impl->renderCount;
    return {};
  } catch (...) {
    return SoftwareError(UIErrorCode::OutOfMemory,
                         "Software frame allocation failed", "Render");
  }
}

auto SoftwareRenderBackend::Present(const RenderSurfaceHandle surface) noexcept
    -> UIResult<void> {
  if (m_impl->FindSurface(surface) == nullptr) {
    return SoftwareError(UIErrorCode::RenderFailed,
                         "Unknown software surface", "Present");
  }
  return {};
}
auto SoftwareRenderBackend::WaitIdle() noexcept -> UIResult<void> { return {}; }

auto SoftwareRenderBackend::Snapshot(
    const RenderSurfaceHandle surface) const noexcept
    -> UIResult<SoftwareSurfaceSnapshot> {
  const auto *record = m_impl->FindSurface(surface);
  if (record == nullptr) {
    return SoftwareError(UIErrorCode::InvalidArgument,
                         "Unknown software surface", "Snapshot");
  }
  try {
    return SoftwareSurfaceSnapshot{record->size, record->rgba,
                                   record->frameNumber};
  } catch (...) {
    return SoftwareError(UIErrorCode::OutOfMemory,
                         "Software snapshot allocation failed", "Snapshot");
  }
}

auto SoftwareRenderBackend::RenderCount() const noexcept -> UInt64 {
  return m_impl->renderCount;
}
auto SoftwareRenderBackend::LiveTextureCount() const noexcept -> UIntSize {
  return static_cast<UIntSize>(std::count_if(
      m_impl->textures.begin(), m_impl->textures.end(),
      [](const auto &entry) { return entry.second.live; }));
}
} // namespace NGIN::UI::Testing
