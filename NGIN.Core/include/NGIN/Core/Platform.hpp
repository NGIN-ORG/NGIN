#pragma once

/// @file Platform.hpp
/// @brief Platform/window/input abstraction contracts hosted under Core `Platform.*` scope.

#include <NGIN/Primitives.hpp>
#include <NGIN/Core/Errors.hpp>

#include <string>

namespace NGIN::Core
{
    /// @brief Window creation descriptor.
    struct WindowDescriptor
    {
        std::string title {};
        NGIN::UInt32 width {1280};
        NGIN::UInt32 height {720};
        bool         resizable {true};
    };

    /// @brief Platform window-system abstraction.
    class IWindowSystem
    {
    public:
        virtual ~IWindowSystem() = default;

        virtual auto CreateMainWindow(const WindowDescriptor& descriptor) noexcept -> CoreResult<void> = 0;
        virtual void PumpEvents() noexcept = 0;
        [[nodiscard]] virtual auto ShouldClose() const noexcept -> bool = 0;
    };

    /// @brief Platform input-system abstraction.
    class IInputSystem
    {
    public:
        virtual ~IInputSystem() = default;

        [[nodiscard]] virtual auto IsKeyDown(NGIN::UInt32 keyCode) const noexcept -> bool = 0;
        [[nodiscard]] virtual auto IsMouseButtonDown(NGIN::UInt32 button) const noexcept -> bool = 0;
    };
}
