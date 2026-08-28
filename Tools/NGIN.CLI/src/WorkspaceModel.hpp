#pragma once

#include "ActionModel.hpp"
#include "ProjectModel.hpp"
#include "SelectionAuthoring.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    struct WorkspaceCapabilityPreference
    {
        std::string name{};
        std::string provider{};
        std::optional<std::string> operatingSystem{};
        std::optional<std::string> architecture{};
        std::optional<std::string> configuration{};
        ManifestSourceRange source{};
    };

    struct WorkspaceProject
    {
        std::filesystem::path path{};
        SemanticProject project{};
        ManifestSourceRange discoveredBy{};
    };

    struct WorkspacePackageSource
    {
        std::string name{};
        std::string kind{};
        std::optional<PortablePath> path{};
        std::optional<std::string> url{};
        ManifestSourceRange source{};
    };

    struct WorkspaceLocalPackage
    {
        std::string name{};
        PortablePath manifest{};
        PortablePath root{};
        PackageCoordinate coordinate{};
        ManifestSourceRange source{};
    };

    struct WorkspacePackageBinding
    {
        std::string package{};
        std::string sourceName{};
        std::string coordinate{};
        ManifestSourceRange source{};
    };

    struct WorkspacePackageProviderPolicy
    {
        std::set<std::string, std::less<>> allowedKinds{};
        bool integrityRequired{false};
        bool locked{false};
        bool allowNonHermetic{true};
    };

    struct WorkspacePathPolicy
    {
        bool allowSymlinks{false};
        bool requireContained{true};
    };

    enum class WorkspaceStageCollisionPolicy
    {
        Error,
        IdenticalBytes,
    };

    struct WorkspaceCompatibilityPolicy
    {
        std::set<std::string, std::less<>> targets{};
        std::set<std::string, std::less<>> toolchains{};
    };

    struct SemanticWorkspace
    {
        AuthoredManifestIdentity manifest{};
        std::string name{};
        std::filesystem::path root{};
        std::vector<WorkspaceProject> projects{};
        WorkspaceSelectionModel selection{};
        std::vector<WorkspaceCapabilityPreference> capabilityPreferences{};
        std::optional<PortablePath> outputRoot{};
        std::map<std::string, WorkspacePackageSource, std::less<>> packageSources{};
        std::map<std::string, WorkspaceLocalPackage, std::less<>> localPackages{};
        std::map<std::string, SourcedVersionConstraint, std::less<>> centralVersions{};
        std::map<std::string, WorkspacePackageBinding, std::less<>> packageBindings{};
        WorkspacePackageProviderPolicy providerPolicy{};
        ActionTrustPolicy actionTrustPolicy{};
        WorkspacePathPolicy pathPolicy{};
        WorkspaceStageCollisionPolicy stageCollision{WorkspaceStageCollisionPolicy::Error};
        WorkspaceCompatibilityPolicy compatibilityPolicy{};
    };

    struct SemanticWorkspaceResult
    {
        std::optional<SemanticWorkspace> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ParseSemanticWorkspace(const AuthoredWorkspaceManifest &workspace) -> SemanticWorkspaceResult;
} // namespace NGIN::CLI
