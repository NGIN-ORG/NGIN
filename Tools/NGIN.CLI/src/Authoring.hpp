#pragma once

#include "Platform.hpp"

namespace NGIN::CLI
{
    [[nodiscard]] auto IsSupportedBuildType(std::string_view value) -> bool;
    [[nodiscard]] auto IsSupportedProjectBuildMode(std::string_view value) -> bool;
    [[nodiscard]] auto IsSupportedProjectType(std::string_view value) -> bool;
    [[nodiscard]] auto IsSupportedOutputKind(std::string_view value) -> bool;
    [[nodiscard]] auto IsValidProjectOutputPairing(std::string_view projectType, std::string_view outputKind) -> bool;
    [[nodiscard]] auto BuiltinConditions() -> std::vector<ConditionDefinition>;
    [[nodiscard]] auto SelectionMatches(const std::vector<ConditionDefinition> &conditions, const SelectorSet &selectors, const ProfileDefinition &profile) -> bool;
    [[nodiscard]] auto SelectionMatches(const ProjectManifest &project, const SelectorSet &selectors, const ProfileDefinition &profile) -> bool;

    [[nodiscard]] auto WorkspaceFilePath(const fs::path &root) -> std::optional<fs::path>;
    [[nodiscard]] auto RootDirFrom(const fs::path &start) -> std::optional<fs::path>;
    [[nodiscard]] auto RootDir(const char *argv0) -> fs::path;

    [[nodiscard]] auto LoadWorkspaceManifest(const fs::path &root) -> WorkspaceManifest;
    [[nodiscard]] auto TryLoadWorkspaceManifest(const fs::path &root) -> std::optional<WorkspaceManifest>;
    [[nodiscard]] auto LoadPackageCatalog(
        const std::optional<WorkspaceManifest> &workspace,
        const fs::path &projectPath) -> std::unordered_map<std::string, PackageCatalogEntry>;
    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest;
    [[nodiscard]] auto LoadLocalSettingsManifest(const fs::path &path) -> LocalSettingsManifest;
    [[nodiscard]] auto LoadProjectManifest(const fs::path &path) -> ProjectManifest;
    [[nodiscard]] auto ProjectWithWorkspacePolicy(
        ProjectManifest project,
        const std::optional<WorkspaceManifest> &workspace) -> ProjectManifest;
    [[nodiscard]] auto ProfileWithWorkspacePolicy(
        const ProjectManifest &project,
        const std::optional<WorkspaceManifest> &workspace,
        const std::optional<std::string> &profileName) -> ProfileDefinition;

    [[nodiscard]] auto FindProjectFile(const fs::path &start) -> std::optional<fs::path>;
    [[nodiscard]] auto ResolveProjectPath(const std::optional<std::string> &explicitPath) -> fs::path;
    [[nodiscard]] auto ProfileByName(const ProjectManifest &project, const std::optional<std::string> &profileName) -> const ProfileDefinition &;
}
