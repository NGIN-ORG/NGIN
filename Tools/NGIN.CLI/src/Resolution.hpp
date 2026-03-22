#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    [[nodiscard]] auto ResolveLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration) -> DiagnosticResult<ResolvedLaunch>;
}
