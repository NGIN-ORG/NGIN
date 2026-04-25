#pragma once

#include "Model.hpp"

#include <cstddef>
#include <vector>

namespace NGIN::CLI
{
    struct MetaGenResult
    {
        bool available{true};
        std::vector<fs::path> generatedFiles{};
        std::size_t reflectedTypeCount{0};
        std::vector<std::string> diagnostics{};
    };

    [[nodiscard]] auto GenerateMetaData(
        const fs::path &root,
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const fs::path &outputDir) -> MetaGenResult;

    [[nodiscard]] auto RunMetaGen(
        const fs::path &root,
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<std::string> &outputPath) -> int;
}
