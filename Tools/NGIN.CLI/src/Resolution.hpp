#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    [[nodiscard]] auto VersionRangeContains(std::string_view range, std::string_view version) -> bool;

    [[nodiscard]] auto ResolveLaunch(
        const ProjectManifest &project,
        const ProfileDefinition &profile,
        const std::optional<WorkspaceManifest> &workspaceOverride = std::nullopt)
        -> DiagnosticResult<ResolvedLaunch>;
}
