#pragma once

#include <NGIN/UI/Theme.hpp>

namespace NGIN::UI {
/// @brief Builds a coordinated set of interaction colors from an opaque RGB
/// accent. Set the theme's surface colors first. The focus ring and selected
/// surface adapt to those colors. The accent's alpha is ignored.
void ApplyAccent(Theme &theme, Color accent) noexcept;
} // namespace NGIN::UI
