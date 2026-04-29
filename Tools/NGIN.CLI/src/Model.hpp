#pragma once

#include "Diagnostics.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace NGIN::CLI
{
    namespace fs = std::filesystem;

    struct WorkspaceManifest
    {
        fs::path path{};
        std::string name{};
        std::string platformVersion{};
        std::vector<fs::path> packageSources{};
        std::unordered_map<std::string, fs::path> packageProviders{};
        std::vector<fs::path> projects{};
    };

    struct PackageDependency
    {
        std::string name{};
        std::string versionRange{};
        bool optional{false};
    };

    struct PackageBootstrapDescriptor
    {
        std::string mode{};
        std::string entryPoint{};
        bool autoApply{false};
    };

    struct LibraryArtifact
    {
        std::string name{};
        std::string target{};
        std::string linkage{};
        std::string origin{};
        bool exported{true};
    };

    struct ExecutableArtifact
    {
        std::string name{};
        std::string target{};
        std::string origin{};
        bool exported{true};
    };

    struct ArtifactDescriptor
    {
        std::vector<LibraryArtifact> libraries{};
        std::vector<ExecutableArtifact> executables{};
    };

    struct SelectorSet
    {
        std::optional<std::string> profile{};
        std::optional<std::string> platform{};
        std::optional<std::string> operatingSystem{};
        std::optional<std::string> architecture{};
        std::optional<std::string> buildType{};
        std::optional<std::string> environment{};
        std::vector<std::string> conditionRefs{};
        bool impossible{false};
    };

    struct ConditionNode
    {
        enum class Kind
        {
            Match,
            All,
            Any,
            Not,
            ConditionRef
        };

        Kind kind{Kind::Match};
        SelectorSet match{};
        std::vector<ConditionNode> children{};
        std::string conditionName{};
    };

    struct ConditionDefinition
    {
        std::string name{};
        ConditionNode body{};
    };

    struct InputMetadataProperty
    {
        std::string name{};
        std::string value{};
    };

    struct InputDeclaration
    {
        std::string name{};
        std::string kind{};
        std::string role{};
        std::string path{};
        std::string pattern{};
        std::string mode{};
        std::string visibility{"Private"};
        std::string target{};
        std::string targetRoot{};
        std::string basePath{};
        std::string contentKind{};
        bool required{true};
        bool overrideExisting{false};
        SelectorSet selectors{};
        std::vector<std::string> includePatterns{};
        std::vector<std::string> excludePatterns{};
        std::string setName{};
        std::string declaringScope{};
        std::vector<InputMetadataProperty> metadata{};
    };

    struct InputRemove
    {
        std::string name{};
        std::string kind{};
        std::string role{};
        std::string path{};
        std::string pattern{};
        std::string mode{};
        std::string visibility{};
        std::string target{};
    };

    struct BuildSetting
    {
        std::string value{};
        std::string visibility{"Private"};
        SelectorSet selectors{};
    };

    struct BuildVariable
    {
        std::string name{};
        std::string value{};
    };

    struct EnvironmentVariable
    {
        std::string name{};
        std::string value{};
        std::string fromEnvironment{};
        std::string fromLocalSetting{};
        bool required{false};
        bool secret{false};
        bool resolved{false};
        std::string resolvedSource{};
    };

    struct LocalSettingsImport
    {
        std::string path{};
        bool optional{false};
    };

    struct LocalSetting
    {
        std::string key{};
        std::string value{};
        bool secret{false};
    };

    struct LocalSettingsManifest
    {
        fs::path path{};
        std::vector<LocalSetting> settings{};
    };

    struct FeatureFlag
    {
        std::string name{};
        bool enabled{false};
    };

    struct CompatibilityDefinition
    {
        std::vector<std::string> operatingSystems{};
        std::vector<std::string> architectures{};
    };

    struct PackageBuildDescriptor
    {
        std::string backend{"CMake"};
        std::string mode{};
        std::vector<BuildVariable> options{};
    };

    struct ProjectBuildDescriptor
    {
        std::string backend{"CMake"};
        std::string mode{"Generated"};
        std::string language{"CXX"};
        std::string languageStandard{"23"};
        bool metaGenEnabled{false};
        std::vector<std::string> sources{};
        std::vector<BuildSetting> includeDirectories{};
        std::vector<BuildSetting> compileDefinitions{};
        std::vector<BuildSetting> compileOptions{};
        std::vector<BuildSetting> linkOptions{};
    };

    struct ModuleDescriptor
    {
        std::string name{};
        std::string family{"App"};
        std::string type{"Runtime"};
        std::string startupStage{"Features"};
        std::string version{};
        std::string compatiblePlatformRange{};
        bool requiresReflection{false};
        CompatibilityDefinition compatibility{};
        std::vector<std::string> required{};
        std::vector<std::string> optional{};
        std::vector<std::string> providesServices{};
        std::vector<std::string> requiresServices{};
        std::vector<std::string> capabilities{};
    };

    struct PluginDescriptor
    {
        std::string name{};
        bool optional{false};
        CompatibilityDefinition compatibility{};
        std::vector<std::string> requiredModules{};
        std::vector<std::string> optionalModules{};
    };

    struct PackageManifest
    {
        fs::path path{};
        std::string name{};
        std::string version{};
        std::string compatiblePlatformRange{};
        ArtifactDescriptor artifacts{};
        PackageBuildDescriptor build{};
        CompatibilityDefinition compatibility{};
        std::vector<PackageDependency> dependencies{};
        std::optional<PackageBootstrapDescriptor> bootstrap{};
        std::vector<InputDeclaration> inputs{};
        std::vector<ModuleDescriptor> modules{};
        std::vector<PluginDescriptor> plugins{};
    };

    struct ResolvedInput
    {
        std::string ownerKind{};
        std::string ownerName{};
        fs::path ownerDirectory{};
        fs::path manifestPath{};
        std::string declaringScope{};
        std::string setName{};
        std::string name{};
        std::string kind{};
        std::string role{};
        std::string source{};
        std::string pattern{};
        std::string mode{};
        std::string visibility{};
        std::string target{};
        std::string targetRoot{};
        std::string basePath{};
        std::string contentKind{};
        bool required{true};
        fs::path absoluteSourcePath{};
        fs::path stagedRelativePath{};
        std::vector<std::string> includePatterns{};
        std::vector<std::string> excludePatterns{};
        std::vector<InputMetadataProperty> metadata{};
    };

    struct ResolvedBootstrap
    {
        std::string packageName{};
        std::string mode{};
        std::string entryPoint{};
        bool autoApply{false};
    };

    struct PackageCatalogEntry
    {
        std::string name{};
        fs::path manifestPath{};
        fs::path providerRoot{};
    };

    struct PackageReference
    {
        std::string name{};
        std::string versionRange{};
        bool optional{false};
    };

    struct ProjectReference
    {
        fs::path path{};
        std::optional<std::string> profile{};
    };

    struct OutputDefinition
    {
        std::string kind{};
        std::string name{};
        std::string target{};
    };

    struct RuntimeDefinition
    {
        std::vector<ModuleDescriptor> modules{};
        std::vector<std::string> enableModules{};
        std::vector<std::string> disableModules{};
        std::vector<std::string> enablePlugins{};
        std::vector<std::string> disablePlugins{};
    };

    struct LaunchDefinition
    {
        std::optional<std::string> executable{};
        std::string workingDirectory{"."};
    };

    struct EnvironmentDefinition
    {
        std::string name{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<InputDeclaration> inputs{};
        std::vector<EnvironmentVariable> variables{};
        std::vector<FeatureFlag> features{};
        RuntimeDefinition runtime{};
    };

    struct ProfileDefinition
    {
        std::string name{};
        std::string buildType{"Debug"};
        std::string platform{"linux-x64"};
        std::string operatingSystem{"linux"};
        std::string architecture{"x64"};
        bool enableReflection{false};
        std::string environmentName{};
        LaunchDefinition launch{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<InputDeclaration> inputs{};
        RuntimeDefinition runtime{};
    };

    struct ProjectManifest
    {
        fs::path path{};
        std::string name{};
        std::string type{};
        std::string defaultProfile{};
        std::vector<InputDeclaration> inputs{};
        std::vector<ConditionDefinition> conditions{};
        OutputDefinition output{};
        ProjectBuildDescriptor build{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<LocalSettingsImport> localSettingsImports{};
        RuntimeDefinition runtime{};
        std::vector<EnvironmentDefinition> environments{};
        std::vector<ProfileDefinition> profiles{};
    };

    struct ResolvedProjectUnit
    {
        ProjectManifest project{};
        ProfileDefinition profile{};
        std::optional<EnvironmentDefinition> environment{};
    };

    struct ResolvedPackage
    {
        PackageManifest manifest{};
        std::string source{"catalog"};
        fs::path sourceDirectory{};
    };

    struct ResolvedLaunch
    {
        std::optional<WorkspaceManifest> workspace{};
        ProjectManifest project{};
        ProfileDefinition profile{};
        std::vector<ResolvedProjectUnit> projectUnits{};
        std::vector<ResolvedInput> inputs{};
        std::vector<EnvironmentVariable> environmentVariables{};
        std::vector<FeatureFlag> environmentFeatures{};
        std::vector<ResolvedBootstrap> bootstraps{};
        std::vector<ResolvedPackage> orderedPackages{};
        std::map<std::string, std::set<std::string>> packageEdges{};
        std::vector<std::string> requiredModules{};
        std::vector<std::string> optionalModules{};
        std::map<std::string, std::set<std::string>> dependencyEdges{};
        std::vector<std::string> enabledPlugins{};
        std::vector<LibraryArtifact> libraries{};
        std::vector<ExecutableArtifact> executables{};
        std::optional<ExecutableArtifact> selectedExecutable{};
    };

    struct GeneratedLaunchPaths
    {
        fs::path outputDir{};
        fs::path manifestPath{};
    };

    struct ConfiguredBuildPaths
    {
        fs::path outputDir{};
        std::optional<fs::path> buildDir{};
        std::optional<fs::path> compileCommandsPath{};
        bool configured{false};
    };

    struct LaunchManifestSummary
    {
        fs::path manifestPath{};
        std::string profileName{};
        std::string workingDirectory{"."};
        std::optional<std::string> selectedExecutable{};
    };
}
