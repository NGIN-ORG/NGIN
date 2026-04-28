#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    [[nodiscard]] auto ResolveLaunch(
        const ProjectManifest &project,
        const ProfileDefinition &profile) -> DiagnosticResult<ResolvedLaunch>;
}
