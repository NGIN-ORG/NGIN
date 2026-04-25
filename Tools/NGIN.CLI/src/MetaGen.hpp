#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    [[nodiscard]] auto RunMetaGen(
        const fs::path &root,
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<std::string> &outputPath) -> int;
}
