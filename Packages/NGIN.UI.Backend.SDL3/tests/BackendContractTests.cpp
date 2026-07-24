#include <NGIN/UI/Backend/SDL3/SDL3.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace {
auto Fail(const char *message) -> int {
  std::cerr << message << '\n';
  return 1;
}
} // namespace

auto main() -> int {
  using namespace NGIN;
  using namespace NGIN::UI;

  auto platform = SDL3::CreatePlatformBackend();
  auto renderer = SDL3::CreateRendererBackend();
  if (!platform || !renderer) {
    return Fail("SDL3 backend factories returned null");
  }
  if (std::string_view{platform->Name()} != "SDL3" ||
      std::string_view{renderer->Name()} != "SDL3/SDL_GPU") {
    return Fail("SDL3 backend names are not stable");
  }
  if (!platform->ContractVersion().Supports(CurrentBackendContractVersion) ||
      !renderer->ContractVersion().Supports(CurrentBackendContractVersion)) {
    return Fail("SDL3 backend contract version is incompatible");
  }
  if (!HasPlatformCapability(platform->Capabilities(),
                             PlatformCapabilityFlags::Clipboard) ||
      !HasPlatformCapability(platform->Capabilities(),
                             PlatformCapabilityFlags::IME) ||
      !HasPlatformCapability(platform->Capabilities(),
                             PlatformCapabilityFlags::MultipleWindows) ||
      !HasRenderCapability(renderer->Capabilities(),
                           RenderCapabilityFlags::TextureUpdates) ||
      !HasRenderCapability(renderer->Capabilities(),
                           RequiredRenderCapabilities)) {
    return Fail("SDL3 backend capabilities are incomplete");
  }

  const TextureCreateInfo textureInfo{
      .size = PixelSize{2, 2},
      .format = TextureFormat::R8,
  };
  if (renderer->CreateTexture(textureInfo)) {
    return Fail("SDL3 renderer accepted a texture before initialization");
  }
  if (!renderer->Initialize(RenderInitInfo{})) {
    return Fail("SDL3 renderer initialization failed");
  }
  auto texture = renderer->CreateTexture(textureInfo);
  if (!texture) {
    return Fail("SDL3 renderer rejected valid CPU texture storage");
  }

  constexpr std::array<Byte, 4> pixels{
      Byte{0x00},
      Byte{0x40},
      Byte{0x80},
      Byte{0xFF},
  };
  if (!renderer->UpdateTexture(texture.Value(),
                               TextureUpdateInfo{
                                   .region = PixelRect{0, 0, 2, 2},
                                   .bytesPerRow = 2,
                                   .bytes = pixels,
                               })) {
    return Fail("SDL3 renderer rejected a valid texture update");
  }
  if (renderer->UpdateTexture(texture.Value(),
                              TextureUpdateInfo{
                                  .region = PixelRect{1, 1, 2, 2},
                                  .bytesPerRow = 2,
                                  .bytes = pixels,
                              })) {
    return Fail("SDL3 renderer accepted an out-of-bounds texture update");
  }
  if (!renderer->DestroyTexture(texture.Value())) {
    return Fail("SDL3 renderer failed to destroy a live texture");
  }
  if (renderer->UpdateTexture(texture.Value(),
                              TextureUpdateInfo{
                                  .region = PixelRect{0, 0, 2, 2},
                                  .bytesPerRow = 2,
                                  .bytes = pixels,
                              })) {
    return Fail("SDL3 renderer accepted a stale texture handle");
  }
  if (!renderer->WaitIdle()) {
    return Fail("SDL3 renderer failed an idle wait without surfaces");
  }
  return 0;
}
