#pragma once

#include <NGIN/UI/Platform.hpp>
#include <NGIN/UI/Rendering.hpp>

#include <memory>

namespace NGIN::UI::SDL3 {
/// @brief Creates the SDL3 native-window and input backend.
[[nodiscard]] auto CreatePlatformBackend() noexcept
    -> std::unique_ptr<IPlatformBackend>;
/// @brief Creates the SDL_GPU render backend.
[[nodiscard]] auto CreateRendererBackend() noexcept
    -> std::unique_ptr<IRenderBackend>;
} // namespace NGIN::UI::SDL3
