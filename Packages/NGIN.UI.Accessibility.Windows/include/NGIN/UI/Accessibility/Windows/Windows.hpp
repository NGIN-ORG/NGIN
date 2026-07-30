#pragma once

#include <NGIN/UI/Accessibility.hpp>

#include <memory>

namespace NGIN::UI::Accessibility::Windows {
/// @brief Creates the Windows UI Automation bridge on Windows.
[[nodiscard]] auto CreateAccessibilityBackend() noexcept
    -> std::unique_ptr<IAccessibilityBackend>;
} // namespace NGIN::UI::Accessibility::Windows
