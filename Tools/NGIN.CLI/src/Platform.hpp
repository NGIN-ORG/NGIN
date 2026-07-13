#pragma once

#include "Model.hpp"

#include <span>
#include <string>
#include <string_view>

namespace NGIN::CLI
{
    struct PlatformIdentity
    {
        std::string name{};
        std::string operatingSystem{};
        std::string architecture{};
    };

    [[nodiscard]] auto IsValidOperatingSystem(std::string_view value) -> bool;
    [[nodiscard]] auto IsValidArchitecture(std::string_view value) -> bool;

    [[nodiscard]] auto DetectHostPlatform() -> PlatformIdentity;

    [[nodiscard]] auto ResolvePlatformIdentity(
        std::string_view name,
        std::span<const WorkspaceManifest::Platform> workspacePlatforms,
        const PlatformIdentity &host) -> PlatformIdentity;

    [[nodiscard]] auto ResolvePlatformIdentity(
        std::string_view name,
        std::span<const WorkspaceManifest::Platform> workspacePlatforms = {}) -> PlatformIdentity;

    auto ApplyTargetPlatform(
        ProfileDefinition &profile,
        std::string_view name,
        std::span<const WorkspaceManifest::Platform> workspacePlatforms = {}) -> void;
}
