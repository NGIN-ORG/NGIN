#pragma once

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Components.hpp>

namespace NGIN::UI::SDL3 {
/// @brief Creates a standalone application with native text and a mounted view.
/// The returned application owns the window, text resources, and mounted
/// content.
[[nodiscard]] auto
Open(WindowCreateInfo window, Views::View content,
     std::unique_ptr<IAccessibilityBackend> accessibility = {})
    -> UIResult<std::unique_ptr<Application>>;

/// @brief Opens a desktop window and runs it until the application exits.
[[nodiscard]] auto
Run(WindowCreateInfo window, Views::View content,
    std::unique_ptr<IAccessibilityBackend> accessibility = {})
    -> UIResult<void>;
} // namespace NGIN::UI::SDL3
