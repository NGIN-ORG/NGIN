#pragma once

#include <NGIN/UI/Platform.hpp>
#include <NGIN/UI/Rendering.hpp>

#include <memory>

/// @brief SDL rendering context supplied by an embedding application.
struct SDL_Renderer;

namespace NGIN::UI::SDL3 {
/// @brief Creates the SDL3 native-window and input backend.
[[nodiscard]] auto CreatePlatformBackend() noexcept
    -> std::unique_ptr<IPlatformBackend>;
/// @brief Creates the SDL_GPU render backend.
[[nodiscard]] auto CreateRendererBackend() noexcept
    -> std::unique_ptr<IRenderBackend>;
/// @brief An initialized UI resource backend and a target borrowing a host renderer.
struct OverlayBackend final {
  std::unique_ptr<IRenderBackend> renderer;
  RenderSurfaceHandle target;
};

/// @brief Draws into the host renderer's current target and pixel-coordinate viewport.
/// Destroy the overlay before destroying the SDL renderer. The host presents.
[[nodiscard]] auto CreateOverlayBackend(SDL_Renderer &host) noexcept
    -> UIResult<OverlayBackend>;
} // namespace NGIN::UI::SDL3
