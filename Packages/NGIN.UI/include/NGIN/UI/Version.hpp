#pragma once

#define NGIN_UI_VERSION_MAJOR 0
#define NGIN_UI_VERSION_MINOR 1
#define NGIN_UI_VERSION_PATCH 0

#define NGIN_UI_DEPRECATED(message) [[deprecated(message)]]

namespace NGIN::UI {
inline constexpr auto VersionMajor = NGIN_UI_VERSION_MAJOR;
inline constexpr auto VersionMinor = NGIN_UI_VERSION_MINOR;
inline constexpr auto VersionPatch = NGIN_UI_VERSION_PATCH;
} // namespace NGIN::UI
