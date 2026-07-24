#pragma once

#include <NGIN/UI/Platform.hpp>
#include <NGIN/UI/Rendering.hpp>

#include <memory>

namespace NGIN::UI::SDL3 {
[[nodiscard]] auto CreatePlatformBackend() noexcept
    -> std::unique_ptr<IPlatformBackend>;
[[nodiscard]] auto CreateRendererBackend() noexcept
    -> std::unique_ptr<IRenderBackend>;
} // namespace NGIN::UI::SDL3
