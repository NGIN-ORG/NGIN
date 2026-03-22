#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    [[nodiscard]] auto IsValidHostProfile(std::string_view value) -> bool;
    [[nodiscard]] auto IsSupportedBuildConfiguration(std::string_view value) -> bool;
    [[nodiscard]] auto IsSupportedProjectBuildMode(std::string_view value) -> bool;

    [[nodiscard]] auto WorkspaceFilePath(const fs::path &root) -> std::optional<fs::path>;
    [[nodiscard]] auto RootDirFrom(const fs::path &start) -> std::optional<fs::path>;
    [[nodiscard]] auto RootDir(const char *argv0) -> fs::path;

    [[nodiscard]] auto LoadWorkspaceManifest(const fs::path &root) -> WorkspaceManifest;
    [[nodiscard]] auto TryLoadWorkspaceManifest(const fs::path &root) -> std::optional<WorkspaceManifest>;
    [[nodiscard]] auto LoadPackageCatalog(
        const std::optional<WorkspaceManifest> &workspace,
        const fs::path &projectPath) -> std::unordered_map<std::string, PackageCatalogEntry>;
    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest;
    [[nodiscard]] auto LoadProjectManifest(const fs::path &path) -> ProjectManifest;

    [[nodiscard]] auto FindProjectFile(const fs::path &start) -> std::optional<fs::path>;
    [[nodiscard]] auto ResolveProjectPath(const std::optional<std::string> &explicitPath) -> fs::path;
    [[nodiscard]] auto ConfigurationByName(const ProjectManifest &project, const std::optional<std::string> &configurationName) -> const ConfigurationDefinition &;
}
