#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    namespace fs = std::filesystem;

    struct CliArguments
    {
        std::optional<std::string> projectPath{};
        std::optional<std::string> workspacePath{};
        std::optional<std::string> outputPath{};
        std::optional<std::string> lockPath{};
        std::optional<std::string> againstPath{};
        std::optional<std::string> format{};
        std::optional<std::string> configuration{};
        std::optional<std::string> target{};
        std::optional<std::string> toolchain{};
        std::optional<std::string> run{};
        std::optional<std::string> profile{};
        std::optional<std::string> packageName{};
        std::optional<std::string> exactVersion{};
        std::optional<std::string> version{};
        std::optional<std::string> atLeastVersion{};
        std::optional<std::string> afterVersion{};
        std::optional<std::string> atMostVersion{};
        std::optional<std::string> beforeVersion{};
        std::optional<std::string> actionKind{};
        std::vector<std::string> files{};
        std::vector<std::string> exportSelections{};
        std::vector<std::string> optionAssignments{};
        std::vector<std::string> positional{};
        std::vector<std::string> trailing{};
        bool quiet{false};
        bool check{false};
        bool effective{false};
    };

    [[nodiscard]] auto ParseCliArguments(int argc, char **argv, int first) -> CliArguments;
    [[nodiscard]] auto FindProject(const fs::path &root, const CliArguments &arguments) -> fs::path;

    auto NewProject(const fs::path &root, std::string_view kind, std::string_view name) -> int;
    auto ValidateManifest(const fs::path &root, const CliArguments &arguments) -> int;
    auto FormatManifest(const fs::path &root, const CliArguments &arguments) -> int;
    auto PrintSchema(const CliArguments &arguments) -> int;
    auto AddPackage(const fs::path &root, const CliArguments &arguments) -> int;
    auto UpdatePackage(const fs::path &root, const CliArguments &arguments) -> int;
    auto RemovePackage(const fs::path &root, const CliArguments &arguments) -> int;
    auto AddProjectReference(const fs::path &root, const CliArguments &arguments) -> int;
    auto AddAction(const fs::path &root, const CliArguments &arguments) -> int;
    auto InspectComposition(const fs::path &root, const CliArguments &arguments) -> int;
    auto DiffComposition(const fs::path &root, const CliArguments &arguments) -> int;
    auto ExplainComposition(const fs::path &root, const CliArguments &arguments) -> int;
    auto ConfigureProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto BuildProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto StageProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto RunProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto TestProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto BenchmarkProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto PublishProject(const fs::path &root, const CliArguments &arguments) -> int;
    auto ExecuteProjectActions(const fs::path &root, const CliArguments &arguments,
                               std::string_view command) -> int;
    auto RestorePackages(const fs::path &root, const CliArguments &arguments) -> int;
    auto WriteDependencyLock(const fs::path &root, const CliArguments &arguments) -> int;
    auto VerifyDependencyLockFile(const fs::path &root, const CliArguments &arguments) -> int;
}
