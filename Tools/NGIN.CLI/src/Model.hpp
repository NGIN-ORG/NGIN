#pragma once

#include "Diagnostics.hpp"

#include <filesystem>
#include <map>
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

    struct ContentFile
    {
        std::string source{};
        std::string kind{};
        std::string target{};
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

    struct BuildSetting
    {
        std::string value{};
        std::string visibility{"Private"};
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
        std::vector<ContentFile> contents{};
        std::vector<ModuleDescriptor> modules{};
        std::vector<PluginDescriptor> plugins{};
    };

    struct ResolvedConfigSource
    {
        std::string ownerProjectName{};
        fs::path ownerProjectDirectory{};
        std::string source{};
        fs::path stagedRelativePath{};
        fs::path absoluteSourcePath{};
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
        std::optional<std::string> configuration{};
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
        std::vector<std::string> configSources{};
        std::vector<ContentFile> contents{};
        std::vector<EnvironmentVariable> variables{};
        std::vector<FeatureFlag> features{};
        RuntimeDefinition runtime{};
    };

    struct ConfigurationDefinition
    {
        std::string name{};
        std::string buildConfiguration{"Debug"};
        std::string operatingSystem{"linux"};
        std::string architecture{"x64"};
        bool enableReflection{false};
        std::string environmentName{};
        LaunchDefinition launch{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<std::string> configSources{};
        RuntimeDefinition runtime{};
    };

    struct ProjectManifest
    {
        fs::path path{};
        std::string name{};
        std::string type{};
        std::string defaultConfiguration{};
        std::vector<std::string> sourceRoots{};
        OutputDefinition output{};
        ProjectBuildDescriptor build{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<std::string> configSources{};
        RuntimeDefinition runtime{};
        std::vector<EnvironmentDefinition> environments{};
        std::vector<ConfigurationDefinition> configurations{};
    };

    struct ResolvedProjectUnit
    {
        ProjectManifest project{};
        ConfigurationDefinition configuration{};
        std::optional<EnvironmentDefinition> environment{};
    };

    struct ResolvedContentSource
    {
        std::string ownerProjectName{};
        fs::path ownerProjectDirectory{};
        std::string source{};
        std::string kind{};
        fs::path stagedRelativePath{};
        fs::path absoluteSourcePath{};
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
        ConfigurationDefinition configuration{};
        std::vector<ResolvedProjectUnit> projectUnits{};
        std::vector<ResolvedConfigSource> configSources{};
        std::vector<ResolvedContentSource> environmentContents{};
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

    struct LaunchManifestSummary
    {
        fs::path manifestPath{};
        std::string configurationName{};
        std::string workingDirectory{"."};
        std::optional<std::string> selectedExecutable{};
    };
}
