#include "GameHost.hpp"
#include "SettingsPanel.hpp"

#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/NativeText.hpp>
#include <NGIN/UI/Surface.hpp>
#include <SDL3/SDL.h>

#include <cmath>
#include <memory>

namespace SettingsDemo {
namespace {
using namespace NGIN;
using namespace NGIN::UI;

class GameInput final : public IPlatformEventSink {
public:
  GameInput(Surface &surface, PlatformWindowHandle window)
      : m_surface(surface), m_window(window) {}

  void Push(PlatformEvent event) override {
    if (EventWindow(event) != m_window) {
      return;
    }
    if (std::holds_alternative<WindowCloseRequested>(event)) {
      running = false;
      return;
    }
    const auto *key = std::get_if<KeyChanged>(&event);
    if (key && key->state == KeyState::Pressed &&
        key->Logical() == LogicalKey::Escape) {
      menuOpen = !menuOpen;
      if (!menuOpen) {
        static_cast<void>(m_surface.Route(WindowFocusChanged{m_window, false}));
      }
      return;
    }
    if (menuOpen) {
      static_cast<void>(m_surface.Route(event));
    } else if (key && key->state == KeyState::Pressed &&
               key->Logical() == LogicalKey::Space) {
      paused = !paused;
    }
  }

  bool running{true};
  bool menuOpen{true};
  bool paused{false};

private:
  Surface &m_surface;
  PlatformWindowHandle m_window;
};

auto RenderScene(SDL_Renderer &renderer, int width, int height, float phase)
    -> bool {
  if (!SDL_SetRenderDrawColor(&renderer, 20, 32, 48, 255) ||
      !SDL_RenderClear(&renderer) ||
      !SDL_SetRenderDrawColor(&renderer, 32, 58, 73, 255)) {
    return false;
  }
  for (float x = phase - 64; x < static_cast<float>(width); x += 64) {
    SDL_FRect bar{x, 0, 12, static_cast<float>(height)};
    if (!SDL_RenderFillRect(&renderer, &bar)) {
      return false;
    }
  }
  return true;
}
} // namespace

auto RunGame(bool smoke) -> UIResult<void> {
  auto platform = SDL3::CreatePlatformBackend();
  if (!platform) {
    return std::unexpected(MakeUIError(UIErrorCode::BackendUnavailable,
                                       "SDL platform unavailable"));
  }
  if (auto init = platform->Initialize({.applicationName = "Settings overlay"});
      !init) {
    return std::unexpected(init.error());
  }
  auto window = platform->CreateWindow(
      {.id = "game",
       .title = "Settings overlay | Esc: menu | Space: pause scene",
       .initialSize = {800, 480}});
  if (!window) {
    return std::unexpected(window.error());
  }
  auto *native = SDL_GetWindowFromID(window->index);
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
      SDL_CreateRenderer(native, nullptr), SDL_DestroyRenderer);
  if (!renderer) {
    return std::unexpected(
        MakeUIError(UIErrorCode::RenderFailed, SDL_GetError()));
  }
  auto overlay = SDL3::CreateOverlayBackend(*renderer);
  if (!overlay) {
    return std::unexpected(overlay.error());
  }
  auto text = NativeTextSystem::Create(*overlay->renderer);
  if (!text) {
    return std::unexpected(text.error());
  }
  Surface surface(
      {.platform = platform.get(), .window = *window, .title = "Settings"});
  (*text)->SetResourcesInvalidatedCallback(
      [invalidate = surface.Invalidator()] {
        invalidate(InvalidationKind::All);
      });
  surface.SetContent(SettingsPanel(), Views::ViewEnvironment{**text});
  GameInput input{surface, *window};
  float phase = 0;
  auto previous = platform->MonotonicNow();
  int frames = 0;
  while (input.running) {
    if (auto events = platform->PollEvents(input); !events) {
      return std::unexpected(events.error());
    }
    if (!input.running) {
      break;
    }
    int width{}, height{};
    if (!SDL_GetRenderOutputSize(renderer.get(), &width, &height)) {
      return std::unexpected(
          MakeUIError(UIErrorCode::RenderFailed, SDL_GetError()));
    }
    const auto now = platform->MonotonicNow();
    if (!input.paused) {
      phase = std::fmod(
          phase + std::chrono::duration<float>(now - previous).count() * 30,
          64.0F);
    }
    previous = now;
    if (width > 0 && height > 0) {
      if (!RenderScene(*renderer, width, height, phase)) {
        return std::unexpected(
            MakeUIError(UIErrorCode::RenderFailed, SDL_GetError()));
      }
      if (input.menuOpen) {
        auto frame = surface.Update(
            {static_cast<UInt32>(width), static_cast<UInt32>(height)},
            SDL_GetWindowPixelDensity(native), now);
        if (!frame) {
          return std::unexpected(frame.error());
        }
        if (auto draw =
                overlay->renderer->Render(overlay->target, (*frame)->View());
            !draw) {
          return std::unexpected(draw.error());
        }
      }
      if (!SDL_RenderPresent(renderer.get())) {
        return std::unexpected(
            MakeUIError(UIErrorCode::RenderFailed, SDL_GetError()));
      }
      if (smoke && ++frames == 3) {
        break;
      }
    }
    SDL_Delay(16);
  }
  return {};
}
} // namespace SettingsDemo
