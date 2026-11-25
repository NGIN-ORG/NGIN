#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/UIRenderer.hpp>
#include <SDL3/SDL.h>

#include <iostream>
#include <memory>

int main() {
  using namespace NGIN::UI;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
      SDL_CreateSurface(32, 24, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!pixels)
    return 1;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> host(
      SDL_CreateSoftwareRenderer(pixels.get()), SDL_DestroyRenderer);
  if (!host)
    return 1;
  SDL_Rect bounds{8, 0, 16, 24};
  if (!SDL_SetRenderDrawColor(host.get(), 255, 0, 0, 255) ||
      !SDL_RenderClear(host.get()) ||
      !SDL_SetRenderClipRect(host.get(), &bounds) ||
      !SDL_SetRenderDrawBlendMode(host.get(), SDL_BLENDMODE_ADD))
    return 1;
  {
    auto overlay = SDL3::CreateOverlayBackend(*host);
    if (!overlay)
      return 1;
    DisplayListBuilder paint;
    paint.Fill({0, 4, 32, 12}, {0, 1, 0, 1});
    auto list = std::move(paint).Finish();
    if (!list)
      return 1;
    auto frame = UIRenderer{}.Build(*list, {32, 24}, 1);
    if (overlay->renderer->Render(overlay->target, frame.View()))
      return 1;
    frame.loadAction = RenderLoadAction::Preserve;
    auto draw = overlay->renderer->Render(overlay->target, frame.View());
    if (!draw) {
      std::cerr << draw.error().message.CStr() << '\n';
      return 1;
    }
    if (overlay->renderer->Present(overlay->target))
      return 1;
  }
  SDL_Rect restored{};
  SDL_BlendMode blend{};
  if (!SDL_GetRenderClipRect(host.get(), &restored) ||
      !SDL_RectsEqual(&bounds, &restored) ||
      !SDL_GetRenderDrawBlendMode(host.get(), &blend) ||
      blend != SDL_BLENDMODE_ADD || !SDL_FlushRenderer(host.get()))
    return 1;
  const auto colorAt = [&](int x, int y, Uint8 r, Uint8 g) {
    Uint8 red{}, green{}, blue{}, alpha{};
    return SDL_ReadSurfacePixel(pixels.get(), x, y, &red, &green, &blue,
                                &alpha) &&
           red == r && green == g && blue == 0 && alpha == 255;
  };
  return colorAt(10, 8, 0, 255) && colorAt(2, 8, 255, 0) &&
                 colorAt(10, 20, 255, 0)
             ? 0
             : 1;
}
