#pragma once

#include "Events.hpp"
#include "Model.hpp"

#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace NGIN::CLI
{
    enum class BackendOutputMode
    {
        Stream,
        Compact,
        Silent,
    };

    struct BackendStepResult
    {
        std::string name{};
        int exitCode{};
        int durationMilliseconds{};
        std::string output{};
    };

    struct BuildExecutionOptions
    {
        BackendOutputMode backendOutput{BackendOutputMode::Stream};
        std::vector<BackendStepResult> *backendSteps{};
        bool interactiveProgress{};
        CliEventEmitter *events{};
    };

    struct ToolResolution
    {
        fs::path path{};
        std::string source{};
    };

    [[nodiscard]] auto ResolveToolPath(
        const std::string &tool,
        const std::optional<fs::path> &searchRoot = std::nullopt) -> std::optional<ToolResolution>;

    [[nodiscard]] auto ToolExists(
        const std::string &tool,
        const std::optional<fs::path> &searchRoot = std::nullopt) -> bool;

    [[nodiscard]] auto RunProcess(
        const fs::path &executable,
        const std::vector<std::string> &arguments,
        const std::optional<fs::path> &workingDirectory = std::nullopt) -> int;

    struct ProcessResult
    {
        int exitCode{};
        std::string output{};
    };

    [[nodiscard]] auto RunProcessCapture(
        const fs::path &executable,
        const std::vector<std::string> &arguments,
        const std::optional<fs::path> &workingDirectory = std::nullopt) -> ProcessResult;

    [[nodiscard]] auto WriteLaunchManifest(
        const ResolvedLaunch &resolved,
        const fs::path &outputDir,
        const std::vector<std::tuple<std::string, fs::path, fs::path>> &staged) -> fs::path;

    auto CleanupPreviousStage(const fs::path &outputDir, DiagnosticReport &report) -> void;

    [[nodiscard]] auto CleanLaunch(
        const ProjectManifest &project,
        const ProfileDefinition &profile,
        const std::optional<fs::path> &outputPath) -> DiagnosticResult<fs::path>;

    [[nodiscard]] auto ConfigureLaunch(
        const ProjectManifest &project,
        const ProfileDefinition &profile,
        const std::optional<fs::path> &outputPath,
        const BuildExecutionOptions &options = BuildExecutionOptions{}) -> DiagnosticResult<ConfiguredBuildPaths>;

    [[nodiscard]] auto BuildLaunch(
        const ProjectManifest &project,
        const ProfileDefinition &profile,
        const std::optional<fs::path> &outputPath,
        const BuildExecutionOptions &options = BuildExecutionOptions{}) -> DiagnosticResult<GeneratedLaunchPaths>;

    [[nodiscard]] auto LoadLaunchManifestSummary(const fs::path &manifestPath) -> LaunchManifestSummary;
}
