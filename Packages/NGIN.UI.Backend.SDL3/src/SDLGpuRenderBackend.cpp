#include <NGIN/UI/Backend/SDL3/SDL3.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::UI::SDL3 {
namespace {
[[nodiscard]] auto SDLError(const UIErrorCode code, const char *operation,
                            const char *fallback) -> std::unexpected<UIError> {
  const auto *message = SDL_GetError();
  return std::unexpected(MakeUIError(
      code, message != nullptr && *message != '\0' ? message : fallback,
      "SDL3/SDL_GPU", operation));
}

[[nodiscard]] constexpr auto BytesPerPixel(const TextureFormat format) noexcept
    -> UIntSize {
  return format == TextureFormat::R8 ? 1 : 4;
}

[[nodiscard]] auto FloatColor(const UInt32 color) noexcept -> SDL_FColor {
  constexpr auto inverse = 1.0F / 255.0F;
  return SDL_FColor{
      static_cast<F32>(color & 0xFFU) * inverse,
      static_cast<F32>((color >> 8U) & 0xFFU) * inverse,
      static_cast<F32>((color >> 16U) & 0xFFU) * inverse,
      static_cast<F32>((color >> 24U) & 0xFFU) * inverse,
  };
}

// The engine retains its clipping and blend state across overlay submissions,
// including early returns from texture uploads or geometry submission.
struct OverlayState {
  SDL_Renderer *renderer{};
  bool clipped{};
  SDL_Rect clip{};
  SDL_BlendMode blend{};
  SDL_FColor color{};
  ~OverlayState() {
    if (renderer) {
      SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
      SDL_SetRenderDrawBlendMode(renderer, blend);
      SDL_SetRenderDrawColorFloat(renderer, color.r, color.g, color.b, color.a);
    }
  }
};

class RenderBackend final : public IRenderBackend {
public:
  ~RenderBackend() override {
    for (auto &[_, surface] : m_surfaces) {
      ReleaseSurface(surface);
    }
  }

  auto Attach(SDL_Renderer &host) -> RenderSurfaceHandle {
    const auto index = m_nextSurface++;
    m_surfaces.emplace(index, SurfaceRecord{.renderer = &host, .borrowed = true});
    m_initialized = true;
    return {index, 1};
  }

  [[nodiscard]] auto Name() const noexcept -> const char * override {
    return "SDL3/SDL_GPU";
  }
  [[nodiscard]] auto ContractVersion() const noexcept
      -> BackendContractVersion override {
    return CurrentBackendContractVersion;
  }
  [[nodiscard]] auto Capabilities() const noexcept
      -> RenderCapabilityFlags override {
    return RenderCapabilityFlags::TextureUpdates |
           RenderCapabilityFlags::ScissorRects | RenderCapabilityFlags::Index32;
  }

  auto Initialize(const RenderInitInfo &) noexcept -> UIResult<void> override {
    m_initialized = true;
    return {};
  }

  auto CreateSurface(const PlatformWindowHandle window,
                     const PixelSize initialSize) noexcept
      -> UIResult<RenderSurfaceHandle> override {
    if (!m_initialized || !window || initialSize.IsEmpty()) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "A live SDL window and non-zero size are required",
                         Name(), "CreateSurface"));
    }
    auto *nativeWindow = SDL_GetWindowFromID(window.index);
    if (nativeWindow == nullptr) {
      return SDLError(UIErrorCode::SurfaceCreationFailed, "CreateSurface",
                      "SDL window lookup failed");
    }
    auto *renderer = SDL_CreateGPURenderer(nullptr, nativeWindow);
    if (renderer == nullptr) {
      return SDLError(UIErrorCode::SurfaceCreationFailed, "CreateSurface",
                      "SDL GPU renderer creation failed");
    }
    const auto index = m_nextSurface++;
    try {
      m_surfaces.emplace(index, SurfaceRecord{
                                    .renderer = renderer,
                                    .size = initialSize,
                                });
    } catch (...) {
      SDL_DestroyRenderer(renderer);
      return std::unexpected(MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL surface tracking allocation failed", Name(),
                         "CreateSurface"));
    }
    return RenderSurfaceHandle{index, 1};
  }

  auto DestroySurface(const RenderSurfaceHandle handle) noexcept
      -> UIResult<void> override {
    auto *surface = FindSurface(handle);
    if (surface == nullptr) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "Unknown SDL GPU surface", Name(), "DestroySurface"));
    }
    ReleaseSurface(*surface);
    return {};
  }

  auto ResizeSurface(const RenderSurfaceHandle handle,
                     const PixelSize size) noexcept -> UIResult<void> override {
    auto *surface = FindSurface(handle);
    if (surface == nullptr || size.IsEmpty()) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "A live surface and non-zero size are required",
                         Name(), "ResizeSurface"));
    }
    surface->size = size;
    return {};
  }

  auto CreateTexture(const TextureCreateInfo &info) noexcept
      -> UIResult<TextureHandle> override {
    if (!m_initialized || info.size.IsEmpty() ||
        info.size.width >
            static_cast<UInt32>(std::numeric_limits<int>::max()) ||
        info.size.height >
            static_cast<UInt32>(std::numeric_limits<int>::max())) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "A valid non-zero texture size is required", Name(),
                         "CreateTexture"));
    }
    try {
      const auto byteCount = static_cast<UIntSize>(info.size.width) *
                             static_cast<UIntSize>(info.size.height) *
                             BytesPerPixel(info.format);
      const auto index = m_nextTexture++;
      m_textures.emplace(index, TextureRecord{
                                    .info = info,
                                    .bytes = std::vector<Byte>(byteCount),
                                });
      return TextureHandle{index, 1};
    } catch (...) {
      return std::unexpected(MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL texture storage allocation failed", Name(),
                         "CreateTexture"));
    }
  }

  auto UpdateTexture(const TextureHandle handle,
                     const TextureUpdateInfo &update) noexcept
      -> UIResult<void> override {
    auto *texture = FindTexture(handle);
    if (texture == nullptr || update.region.width == 0 ||
        update.region.height == 0 || update.region.x < 0 ||
        update.region.y < 0) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "Unknown texture or invalid update region", Name(),
                         "UpdateTexture"));
    }
    const auto x = static_cast<UInt32>(update.region.x);
    const auto y = static_cast<UInt32>(update.region.y);
    if (x > texture->info.size.width ||
        update.region.width > texture->info.size.width - x ||
        y > texture->info.size.height ||
        update.region.height > texture->info.size.height - y) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "Texture update exceeds its allocation", Name(),
                         "UpdateTexture"));
    }
    const auto bytesPerPixel = BytesPerPixel(texture->info.format);
    const auto sourceRowBytes =
        static_cast<UIntSize>(update.region.width) * bytesPerPixel;
    const auto precedingRows = static_cast<UIntSize>(update.region.height - 1);
    if (precedingRows > 0 &&
        update.bytesPerRow >
            (std::numeric_limits<UIntSize>::max() - sourceRowBytes) /
                precedingRows) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "Texture update byte layout overflows", Name(),
                         "UpdateTexture"));
    }
    const auto required = update.bytesPerRow * precedingRows + sourceRowBytes;
    if (update.bytesPerRow < sourceRowBytes || update.bytes.size() < required) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                         "Texture update byte span is too small", Name(),
                         "UpdateTexture"));
    }
    for (UInt32 row = 0; row < update.region.height; ++row) {
      const auto sourceOffset = static_cast<UIntSize>(row) * update.bytesPerRow;
      const auto destinationOffset =
          (static_cast<UIntSize>(y + row) *
               static_cast<UIntSize>(texture->info.size.width) +
           x) *
          bytesPerPixel;
      std::copy_n(update.bytes.begin() +
                      static_cast<std::ptrdiff_t>(sourceOffset),
                  static_cast<std::ptrdiff_t>(sourceRowBytes),
                  texture->bytes.begin() +
                      static_cast<std::ptrdiff_t>(destinationOffset));
    }
    ++texture->revision;
    return {};
  }

  auto DestroyTexture(const TextureHandle handle) noexcept
      -> UIResult<void> override {
    auto *texture = FindTexture(handle);
    if (texture == nullptr) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument, "Unknown SDL texture",
                         Name(), "DestroyTexture"));
    }
    texture->live = false;
    texture->bytes.clear();
    for (auto &[_, surface] : m_surfaces) {
      if (const auto found = surface.textures.find(handle.index);
          found != surface.textures.end()) {
        SDL_DestroyTexture(found->second.texture);
        surface.textures.erase(found);
      }
    }
    return {};
  }

  auto Render(const RenderSurfaceHandle handle,
              const RenderPacket &packet) noexcept -> UIResult<void> override {
    auto *surface = FindSurface(handle);
    if (surface == nullptr) {
      return std::unexpected(MakeUIError(UIErrorCode::RenderFailed, "Unknown SDL GPU surface",
                         Name(), "Render"));
    }
    if (surface->borrowed && packet.loadAction != RenderLoadAction::Preserve) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
          "A host-owned overlay requires a preserving render packet", Name(), "Render"));
    }
    try {
      for (const auto &update : packet.textureUpdates) {
        auto result = UpdateTexture(update.texture, update.update);
        if (!result) {
          return std::unexpected(result.error());
        }
      }
      if (packet.vertices.size() >
          static_cast<UIntSize>(std::numeric_limits<int>::max())) {
        return std::unexpected(MakeUIError(UIErrorCode::RenderFailed,
                           "Render packet exceeds SDL geometry limits", Name(),
                           "Render"));
      }
      OverlayState previous{};
      if (surface->borrowed) {
        previous.clipped = SDL_RenderClipEnabled(surface->renderer);
        if (!SDL_GetRenderClipRect(surface->renderer, &previous.clip) ||
            !SDL_GetRenderDrawBlendMode(surface->renderer, &previous.blend) ||
            !SDL_GetRenderDrawColorFloat(surface->renderer, &previous.color.r,
                &previous.color.g, &previous.color.b, &previous.color.a)) {
          return SDLError(UIErrorCode::RenderFailed, "Render", "Could not read host render state");
        }
        previous.renderer = surface->renderer;
      }
      if (packet.loadAction == RenderLoadAction::Clear &&
          (!SDL_SetRenderDrawColorFloat(
              surface->renderer, packet.clearColor.red, packet.clearColor.green,
              packet.clearColor.blue, packet.clearColor.alpha) ||
          !SDL_RenderClear(surface->renderer))) {
        return SDLError(UIErrorCode::RenderFailed, "Clear",
                        "SDL GPU clear failed");
      }
      auto &colors = surface->colors;
      colors.clear();
      colors.reserve(packet.vertices.size());
      for (const auto &vertex : packet.vertices) {
        colors.push_back(FloatColor(vertex.color));
      }
      for (const auto &batch : packet.batches) {
        const auto firstIndex = static_cast<UIntSize>(batch.firstIndex);
        const auto indexCount = static_cast<UIntSize>(batch.indexCount);
        if (firstIndex > packet.indices.size() ||
            indexCount > packet.indices.size() - firstIndex ||
            batch.indexCount >
                static_cast<UInt32>(std::numeric_limits<int>::max()) ||
            batch.scissor.width >
                static_cast<UInt32>(std::numeric_limits<int>::max()) ||
            batch.scissor.height >
                static_cast<UInt32>(std::numeric_limits<int>::max())) {
          return std::unexpected(MakeUIError(UIErrorCode::RenderFailed,
                             "Render batch index range is invalid", Name(),
                             "Render"));
        }
        SDL_Rect clip{
            batch.scissor.x,
            batch.scissor.y,
            static_cast<int>(batch.scissor.width),
            static_cast<int>(batch.scissor.height),
        };
        if (previous.clipped) {
          SDL_Rect intersection{};
          if (!SDL_GetRectIntersection(&previous.clip, &clip, &intersection)) {
            continue;
          }
          clip = intersection;
        }
        if (!SDL_SetRenderClipRect(surface->renderer, &clip)) {
          return SDLError(UIErrorCode::RenderFailed, "SetScissor",
                          "SDL GPU scissor update failed");
        }
        SDL_Texture *texture = nullptr;
        if (batch.texture) {
          auto synchronized = SynchronizeTexture(*surface, batch.texture);
          if (!synchronized) {
            return std::unexpected(synchronized.error());
          }
          texture = synchronized.value();
          static_cast<void>(SDL_SetTextureBlendMode(
              texture, batch.blendMode == BlendMode::Opaque
                           ? SDL_BLENDMODE_NONE
                           : SDL_BLENDMODE_BLEND_PREMULTIPLIED));
        } else {
          static_cast<void>(SDL_SetRenderDrawBlendMode(
              surface->renderer, batch.blendMode == BlendMode::Opaque
                                     ? SDL_BLENDMODE_NONE
                                     : SDL_BLENDMODE_BLEND_PREMULTIPLIED));
        }
        if (!packet.vertices.empty() &&
            !SDL_RenderGeometryRaw(
                surface->renderer, texture, &packet.vertices.front().x,
                static_cast<int>(sizeof(RenderVertex)), colors.data(),
                static_cast<int>(sizeof(SDL_FColor)),
                &packet.vertices.front().u,
                static_cast<int>(sizeof(RenderVertex)),
                static_cast<int>(packet.vertices.size()),
                packet.indices.data() + batch.firstIndex,
                static_cast<int>(batch.indexCount),
                static_cast<int>(sizeof(UInt32)))) {
          return SDLError(UIErrorCode::RenderFailed, "RenderGeometry",
                          "SDL GPU geometry submission failed");
        }
      }
      static_cast<void>(SDL_SetRenderClipRect(surface->renderer, nullptr));
      return {};
    } catch (...) {
      return std::unexpected(MakeUIError(UIErrorCode::OutOfMemory,
                         "SDL render staging allocation failed", Name(),
                         "Render"));
    }
  }

  auto Present(const RenderSurfaceHandle handle) noexcept
      -> UIResult<void> override {
    auto *surface = FindSurface(handle);
    if (surface && surface->borrowed) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidState,
          "The host presents its overlay target", Name(), "Present"));
    }
    if (surface == nullptr || !SDL_RenderPresent(surface->renderer)) {
      return SDLError(UIErrorCode::RenderFailed, "Present",
                      "SDL GPU present failed");
    }
    return {};
  }

  auto WaitIdle() noexcept -> UIResult<void> override {
    for (auto &[_, surface] : m_surfaces) {
      if (surface.renderer != nullptr) {
        auto *device = SDL_GetGPURendererDevice(surface.renderer);
        if (device != nullptr && !SDL_WaitForGPUIdle(device)) {
          return SDLError(UIErrorCode::RenderFailed, "WaitIdle",
                          "SDL GPU idle wait failed");
        }
      }
    }
    return {};
  }

private:
  struct SurfaceTexture final {
    SDL_Texture *texture{nullptr};
    UInt64 revision{0};
  };
  struct SurfaceRecord final {
    SDL_Renderer *renderer{nullptr};
    PixelSize size{};
    bool borrowed{false};
    std::vector<SDL_FColor> colors{};
    std::unordered_map<UInt32, SurfaceTexture> textures{};
  };
  static void ReleaseSurface(SurfaceRecord &surface) noexcept {
    if (surface.borrowed) {
      for (auto &[_, entry] : surface.textures) {
        SDL_DestroyTexture(entry.texture);
      }
    } else if (surface.renderer) {
      SDL_DestroyRenderer(surface.renderer);
    }
    surface.textures.clear();
    surface.renderer = nullptr;
  }
  struct TextureRecord final {
    TextureCreateInfo info{};
    std::vector<Byte> bytes{};
    UInt64 revision{1};
    bool live{true};
  };

  [[nodiscard]] auto FindSurface(const RenderSurfaceHandle handle) noexcept
      -> SurfaceRecord * {
    const auto found = m_surfaces.find(handle.index);
    return handle.generation == 1 && found != m_surfaces.end() &&
                   found->second.renderer != nullptr
               ? &found->second
               : nullptr;
  }
  [[nodiscard]] auto FindTexture(const TextureHandle handle) noexcept
      -> TextureRecord * {
    const auto found = m_textures.find(handle.index);
    return handle.generation == 1 && found != m_textures.end() &&
                   found->second.live
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] auto SynchronizeTexture(SurfaceRecord &surface,
                                        const TextureHandle handle)
      -> UIResult<SDL_Texture *> {
    auto *record = FindTexture(handle);
    if (record == nullptr) {
      return std::unexpected(MakeUIError(UIErrorCode::RenderFailed, "Unknown SDL texture",
                         Name(), "SynchronizeTexture"));
    }
    auto &surfaceTexture = surface.textures[handle.index];
    if (surfaceTexture.texture == nullptr) {
      const auto format = record->info.format == TextureFormat::BGRA8
                              ? SDL_PIXELFORMAT_BGRA32
                              : SDL_PIXELFORMAT_RGBA32;
      surfaceTexture.texture =
          SDL_CreateTexture(surface.renderer, format, SDL_TEXTUREACCESS_STATIC,
                            static_cast<int>(record->info.size.width),
                            static_cast<int>(record->info.size.height));
      if (surfaceTexture.texture == nullptr) {
        return SDLError(UIErrorCode::ResourceFailed, "CreateTexture",
                        "SDL texture creation failed");
      }
      const auto scaleMode = record->info.filter == TextureFilter::Nearest
                                 ? SDL_SCALEMODE_NEAREST
                                 : SDL_SCALEMODE_LINEAR;
      if (!SDL_SetTextureScaleMode(surfaceTexture.texture, scaleMode)) {
        SDL_DestroyTexture(surfaceTexture.texture);
        surfaceTexture.texture = nullptr;
        return SDLError(UIErrorCode::ResourceFailed, "SetTextureScaleMode",
                        "SDL texture filtering setup failed");
      }
    }
    if (surfaceTexture.revision != record->revision) {
      if (record->info.format == TextureFormat::R8) {
        std::vector<UInt8> rgba(record->bytes.size() * 4);
        for (UIntSize index = 0; index < record->bytes.size(); ++index) {
          const auto alpha = static_cast<UInt8>(record->bytes[index]);
          rgba[index * 4] = alpha;
          rgba[index * 4 + 1] = alpha;
          rgba[index * 4 + 2] = alpha;
          rgba[index * 4 + 3] = alpha;
        }
        if (!SDL_UpdateTexture(
                surfaceTexture.texture, nullptr, rgba.data(),
                static_cast<int>(record->info.size.width * 4U))) {
          return SDLError(UIErrorCode::ResourceFailed, "UpdateTexture",
                          "SDL glyph texture upload failed");
        }
      } else if (!SDL_UpdateTexture(
                     surfaceTexture.texture, nullptr, record->bytes.data(),
                     static_cast<int>(record->info.size.width * 4U))) {
        return SDLError(UIErrorCode::ResourceFailed, "UpdateTexture",
                        "SDL texture upload failed");
      }
      surfaceTexture.revision = record->revision;
    }
    return surfaceTexture.texture;
  }

  bool m_initialized{false};
  UInt32 m_nextSurface{0};
  UInt32 m_nextTexture{0};
  std::unordered_map<UInt32, SurfaceRecord> m_surfaces{};
  std::unordered_map<UInt32, TextureRecord> m_textures{};
};
} // namespace

auto CreateRendererBackend() noexcept -> std::unique_ptr<IRenderBackend> {
  try {
    return std::make_unique<RenderBackend>();
  } catch (...) {
    return {};
  }
}
auto CreateOverlayBackend(SDL_Renderer &host) noexcept -> UIResult<OverlayBackend> {
  try {
    auto renderer = std::make_unique<RenderBackend>();
    auto target = renderer->Attach(host);
    return OverlayBackend{std::move(renderer), target};
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeUIError(UIErrorCode::OutOfMemory,
        "Could not allocate an SDL overlay backend", "SDL3", "CreateOverlayBackend"));
  }
}
} // namespace NGIN::UI::SDL3
