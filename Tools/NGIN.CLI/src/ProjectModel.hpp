#pragma once

#include "ActionContract.hpp"
#include "CompositionBoundary.hpp"
#include "ManifestPaths.hpp"
#include "Selection.hpp"
#include "SemanticMerge.hpp"

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace NGIN::CLI
{
    enum class ProductArtifactKind
    {
        Executable,
        Library,
    };

    enum class LibraryKind
    {
        None,
        Static,
        Shared,
        Interface,
        Plugin,
    };

    enum class DependencyContext
    {
        Target,
        Test,
        Benchmark,
        Publish,
    };

    enum class ExportUseKind
    {
        Library,
        Tool,
        Plugin,
        Action,
        Asset,
    };

    struct ExportUse
    {
        ExportUseKind kind{ExportUseKind::Library};
        std::string name{};
        ManifestSourceRange source{};
    };

    struct PackageDependencyRequest
    {
        std::string name{};
        std::optional<std::string> sourceBinding{};
        std::optional<SourcedVersionConstraint> constraint{};
        std::vector<ExportUse> exports{};
        std::map<std::string, std::string, std::less<>> optionAssignments{};
        DependencyContext context{DependencyContext::Target};
        std::optional<std::string> owner{};
        ManifestSourceRange source{};
    };

    struct ProjectDependencyRequest
    {
        std::string name{};
        std::optional<PortablePath> path{};
        DependencyContext context{DependencyContext::Target};
        std::optional<std::string> owner{};
        ManifestSourceRange source{};
    };

    struct ProjectCapabilityRequest
    {
        std::string name{};
        std::string domain{"Link"};
        std::optional<SourcedVersionConstraint> constraint{};
        std::optional<std::string> provider{};
        DependencyContext context{DependencyContext::Target};
        ManifestSourceRange source{};
    };

    using ProjectDependency =
        std::variant<PackageDependencyRequest, ProjectDependencyRequest, ProjectCapabilityRequest>;

    enum class BuildItemKind
    {
        Source,
        Header,
        CxxModule,
        Resource,
        IncludeDirectory,
        Define,
        CompileOption,
        LinkOption,
        PrecompiledHeader,
    };

    enum class BuildItemOperation
    {
        Include,
        Remove,
        Update,
    };

    enum class BuildVisibility
    {
        Private,
        Public,
        Interface,
    };

    struct LanguageRequirement
    {
        std::string standard{"C++23"};
        bool extensions{false};
        bool required{true};
        ManifestSourceRange source{};
    };

    struct UnityBuildSetting
    {
        bool enabled{false};
        std::optional<std::int64_t> batchSize{};
        ManifestSourceRange source{};
    };

    struct BuildItemDeclaration
    {
        BuildItemKind kind{BuildItemKind::Source};
        BuildItemOperation operation{BuildItemOperation::Include};
        std::string pattern{};
        std::optional<std::string> exclude{};
        std::optional<PortablePath> destination{};
        std::optional<BuildVisibility> visibility{};
        std::string detail{};
        std::optional<std::string> value{};
        std::optional<bool> generated{};
        std::optional<bool> system{};
        bool allowEmpty{false};
        ManifestSourceRange source{};
    };

    struct ProjectBuildModel
    {
        bool conventions{true};
        std::map<std::string, bool, std::less<>> namedConventions{};
        LanguageRequirement language{};
        std::optional<UnityBuildSetting> unityBuild{};
        std::vector<BuildItemDeclaration> declarations{};
    };

    enum class BuildItemOriginKind
    {
        Convention,
        Authored,
        Updated,
    };

    struct ResolvedBuildItem
    {
        BuildItemKind kind{BuildItemKind::Source};
        std::string identity{};
        PortablePath path{};
        std::optional<PortablePath> destination{};
        BuildVisibility visibility{BuildVisibility::Private};
        std::string detail{};
        std::optional<std::string> value{};
        bool generated{false};
        bool system{false};
        BuildItemOriginKind origin{BuildItemOriginKind::Authored};
        ManifestSourceRange source{};

        [[nodiscard]] friend auto operator==(const ResolvedBuildItem &, const ResolvedBuildItem &) -> bool = default;
    };

    struct ResolvedProjectBuild
    {
        LanguageRequirement language{};
        std::optional<UnityBuildSetting> unityBuild{};
        std::vector<ResolvedBuildItem> items{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct ProjectMetadata
    {
        std::optional<std::string> description{};
        std::optional<std::string> license{};
        std::optional<std::string> homepage{};
        std::optional<std::string> vendor{};
    };

    struct ProjectActionInput
    {
        std::string include{};
        std::optional<std::string> exclude{};
        ManifestSourceRange source{};
    };

    struct ProjectActionSelection
    {
        ActionKind kind{ActionKind::Custom};
        std::string qualifiedAction{};
        std::vector<ProjectActionInput> inputs{};
        std::map<std::string, std::string, std::less<>> options{};
        std::vector<std::string> arguments{};
        ManifestSourceRange source{};
    };

    enum class StageInputKind
    {
        File,
        Directory,
    };

    struct ProjectStageInput
    {
        StageInputKind kind{StageInputKind::File};
        PortablePath include{};
        PortablePath destination{};
        ManifestSourceRange source{};
    };

    struct ProjectRunDefinition
    {
        std::string name{};
        bool defaultRun{false};
        std::optional<std::string> product{};
        std::optional<std::string> tool{};
        PortablePath workingDirectory{.value = ".", .base = PortablePathBase::Manifest};
        std::vector<std::string> arguments{};
        std::map<std::string, std::string, std::less<>> environment{};
        std::map<std::string, std::string, std::less<>> secrets{};
        bool implicit{false};
        ManifestSourceRange source{};
    };

    struct ProjectTestingDefinition
    {
        std::string name{};
        std::vector<std::string> arguments{};
        std::map<std::string, std::string, std::less<>> environment{};
        std::optional<std::int64_t> timeoutSeconds{};
        ManifestSourceRange source{};
    };

    struct ProjectBenchmarkDefinition : ProjectTestingDefinition
    {
        std::optional<std::int64_t> repetitions{};
        std::optional<std::int64_t> warmupSeconds{};
    };

    enum class PublishOutputKind
    {
        Folder,
        Archive,
        Installer,
    };

    struct ProjectPublishDefinition
    {
        std::string name{};
        PublishOutputKind kind{PublishOutputKind::Folder};
        std::string format{};
        PortablePath output{};
        ManifestSourceRange source{};
    };

    struct ProjectBuildRefinement
    {
        std::optional<SourcedAssignment<bool>> conventions{};
        std::optional<LanguageRequirement> language{};
        std::optional<UnityBuildSetting> unityBuild{};
        std::map<std::string, SourcedAssignment<bool>, std::less<>> namedConventions{};
        std::vector<BuildItemDeclaration> declarations{};
    };

    struct ProjectRefinement
    {
        SemanticRefinement semantic{};
        ProjectBuildRefinement build{};
        std::vector<ProjectDependency> dependencies{};
        std::vector<ProjectStageInput> stage{};
    };

    struct SemanticProject
    {
        AuthoredManifestIdentity manifest{};
        std::string name{};
        ProductArtifactKind artifactKind{ProductArtifactKind::Executable};
        LibraryKind libraryKind{LibraryKind::None};
        std::optional<SemanticVersion> version{};
        ProjectMetadata metadata{};
        std::map<std::string, OptionDefinition, std::less<>> options{};
        std::vector<ProjectDependency> dependencies{};
        std::vector<ProjectActionSelection> actions{};
        std::vector<ProjectStageInput> stage{};
        std::vector<ProjectRunDefinition> runs{};
        std::vector<ProjectTestingDefinition> tests{};
        std::vector<ProjectBenchmarkDefinition> benchmarks{};
        std::vector<ProjectPublishDefinition> publishes{};
        std::vector<ProjectRefinement> refinements{};
        ProjectBuildModel build{};
        bool hasBuildSection{false};
    };

    struct SemanticProjectResult
    {
        std::optional<SemanticProject> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ProductArtifactKindName(ProductArtifactKind kind) -> std::string_view;
    [[nodiscard]] auto LibraryKindName(LibraryKind kind) -> std::string_view;
    [[nodiscard]] auto ParseSemanticProject(const AuthoredProjectManifest &project) -> SemanticProjectResult;
    [[nodiscard]] auto ApplyProjectRefinements(const SemanticProject &project, const SelectionFacts &selection)
        -> SemanticProjectResult;
    [[nodiscard]] auto ResolveProjectBuild(const SemanticProject &project,
                                           const std::filesystem::path &projectDirectory,
                                           bool targetCaseInsensitive = false, bool allowSymlinks = false)
        -> ResolvedProjectBuild;
} // namespace NGIN::CLI
