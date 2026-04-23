#pragma once

#include "Model.hpp"

#include <optional>
#include <tuple>
#include <vector>

namespace NGIN::CLI
{
    [[nodiscard]] auto ToolExists(const std::string &tool) -> bool;

    [[nodiscard]] auto RunProcess(
        const fs::path &executable,
        const std::vector<std::string> &arguments,
        const std::optional<fs::path> &workingDirectory = std::nullopt) -> int;

    [[nodiscard]] auto WriteLaunchManifest(
        const ResolvedLaunch &resolved,
        const fs::path &outputDir,
        const std::vector<std::tuple<std::string, fs::path, fs::path>> &staged) -> fs::path;

    auto CleanupPreviousStage(const fs::path &outputDir, DiagnosticReport &report) -> void;

    [[nodiscard]] auto CleanLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<fs::path> &outputPath) -> DiagnosticResult<fs::path>;

    [[nodiscard]] auto BuildLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration,
        const std::optional<fs::path> &outputPath) -> DiagnosticResult<GeneratedLaunchPaths>;

    [[nodiscard]] auto LoadLaunchManifestSummary(const fs::path &manifestPath) -> LaunchManifestSummary;
}
