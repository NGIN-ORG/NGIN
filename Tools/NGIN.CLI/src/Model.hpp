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
        struct Platform
        {
            std::string name{};
            std::string operatingSystem{};
            std::string architecture{};
            std::string abi{};
        };

        struct Toolchain
        {
            std::string name{};
            std::string compiler{};
            std::string compilerVersion{};
            std::string linker{};
            std::string generator{};
            std::string cppStandardLibrary{};
            std::string runtimeLibrary{};
        };

        struct ProfilePolicy
        {
            struct BuildSettingPolicy
            {
                std::string productKind{};
                std::string kind{};
                std::string value{};
                std::string visibility{"Private"};
            };

            struct AnalyzerPolicy
            {
                std::string productKind{};
                std::string name{};
                std::string scope{"Build"};
                bool enabled{true};
                std::string severity{"Warning"};
                std::string configPath{};
            };

            struct EnvironmentVariablePolicy
            {
                std::string productKind{};
                std::string name{};
                std::string value{};
                std::string fromLocalSetting{};
                bool required{false};
                bool secret{false};
                bool remove{false};
            };

            struct StageInputPolicy
            {
                std::string productKind{};
                std::string kind{};
                std::string source{};
                std::string target{};
                std::string collision{};
                bool remove{false};
            };

            struct DependencyUsePolicy
            {
                std::string productKind{};
                std::string kind{};
                std::string name{};
                fs::path path{};
                std::string versionRange{};
                std::string scope{};
                std::string removeScope{};
                std::vector<std::string> features{};
                bool remove{false};
            };

            struct RuntimeModulePolicy
            {
                std::string productKind{};
                std::string name{};
                std::string stage{"Features"};
                std::vector<std::string> providesServices{};
                std::vector<std::string> requiresServices{};
                bool remove{false};
            };

            std::string name{};
            std::optional<std::string> buildType{};
            std::optional<std::string> hostPlatform{};
            std::optional<std::string> targetPlatform{};
            std::optional<std::string> operatingSystem{};
            std::optional<std::string> architecture{};
            std::optional<std::string> environmentName{};
            std::optional<std::string> toolchain{};
            std::optional<std::string> language{};
            std::optional<std::string> languageStandard{};
            std::optional<std::string> backend{};
            std::optional<std::string> buildMode{};
            std::vector<BuildSettingPolicy> buildSettings{};
            std::vector<AnalyzerPolicy> analyzers{};
            std::vector<EnvironmentVariablePolicy> environmentVariables{};
            std::vector<StageInputPolicy> stageInputs{};
            std::vector<DependencyUsePolicy> dependencyUses{};
            std::vector<RuntimeModulePolicy> runtimeModules{};
        };

        fs::path path{};
        std::string name{};
        std::string defaultProfile{};
        std::string platformVersion{};
        ProfilePolicy defaults{};
        std::vector<ProfilePolicy> profiles{};
        std::vector<fs::path> imports{};
        std::vector<Platform> platforms{};
        std::vector<Toolchain> toolchains{};
        std::vector<fs::path> packageSources{};
        std::vector<std::string> packageSourceUrls{};
        std::unordered_map<std::string, fs::path> packageProviders{};
        std::unordered_map<std::string, std::string> dependencyVersions{};
        std::string versionResolution{"HighestCompatible"};
        std::string defaultFeatures{"Explicit"};
        std::string lockFile{"Optional"};
        std::vector<fs::path> projects{};
    };

    struct PackageDependency
    {
        std::string name{};
        std::string versionRange{};
        bool optional{false};
        std::string scope{};
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

    struct PackageReference
    {
        std::string name{};
        std::string versionRange{};
        bool optional{false};
        bool disabled{false};
        SelectorSet selectors{};
        std::string scope{};
        std::string removeScope{};
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
        fs::path manifestPath{};
        std::string sourceKind{};
        std::string sourceName{};
        bool builtin{false};
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
        SelectorSet selectors{};
    };

    struct PackageFeatureUse
    {
        std::string packageName{};
        std::string featureName{};
        std::string versionRange{};
        bool disabled{false};
        SelectorSet selectors{};
    };

    struct CapabilityRequirement
    {
        std::string name{};
    };

    struct CapabilityProvision
    {
        std::string name{};
        bool exclusive{false};
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
        bool backendExplicit{false};
        bool languageExplicit{false};
        std::vector<std::string> sources{};
        std::vector<BuildSetting> includeDirectories{};
        std::vector<BuildSetting> compileDefinitions{};
        std::vector<BuildSetting> compileOptions{};
        std::vector<BuildSetting> linkOptions{};
    };

    struct ToolDeclaration
    {
        std::string name{};
        std::string kind{"Generator"};
        std::string executable{};
        SelectorSet selectors{};
    };

    struct GeneratorArgument
    {
        std::string value{};
        std::string path{};
        SelectorSet selectors{};
    };

    struct GeneratorDeclaration
    {
        std::string name{};
        std::string kind{};
        std::string packageName{};
        std::string toolName{};
        ToolDeclaration inlineTool{};
        bool hasInlineTool{false};
        bool disabled{false};
        SelectorSet selectors{};
        std::vector<GeneratorArgument> arguments{};
        std::vector<InputDeclaration> inputs{};
        std::vector<InputDeclaration> outputs{};
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
        SelectorSet selectors{};
    };

    struct PluginDescriptor
    {
        std::string name{};
        bool optional{false};
        CompatibilityDefinition compatibility{};
        std::vector<std::string> requiredModules{};
        std::vector<std::string> optionalModules{};
        SelectorSet selectors{};
    };

    struct ProjectReference
    {
        fs::path path{};
        std::optional<std::string> profile{};
        bool disabled{false};
        SelectorSet selectors{};
    };

    struct RuntimeReference
    {
        std::string name{};
        SelectorSet selectors{};
    };

    struct RuntimeDefinition
    {
        std::vector<ModuleDescriptor> modules{};
        std::vector<RuntimeReference> enableModules{};
        std::vector<RuntimeReference> disableModules{};
        std::vector<RuntimeReference> enablePlugins{};
        std::vector<RuntimeReference> disablePlugins{};
    };

    struct PackageManifest
    {
        fs::path path{};
        std::string name{};
        std::string version{};
        std::string compatiblePlatformRange{};
        std::string defaultFeatures{"Explicit"};
        std::string lockFile{"Optional"};
        ArtifactDescriptor artifacts{};
        PackageBuildDescriptor build{};
        CompatibilityDefinition compatibility{};
        std::vector<PackageDependency> dependencies{};
        std::optional<PackageBootstrapDescriptor> bootstrap{};
        std::vector<InputDeclaration> inputs{};
        std::vector<ConditionDefinition> conditions{};
        std::vector<ModuleDescriptor> modules{};
        std::vector<PluginDescriptor> plugins{};
        std::vector<ToolDeclaration> tools{};
        struct Feature
        {
            std::string name{};
            std::string description{};
            SelectorSet selectors{};
            std::vector<CapabilityProvision> provides{};
            std::vector<CapabilityRequirement> requiredCapabilities{};
            std::vector<PackageReference> packageRefs{};
            std::vector<InputDeclaration> inputs{};
            ProjectBuildDescriptor build{};
            RuntimeDefinition runtime{};
            std::vector<EnvironmentVariable> variables{};
            std::vector<GeneratorDeclaration> generators{};
        };
        std::vector<Feature> features{};
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

    struct OutputDefinition
    {
        std::string kind{};
        std::string name{};
        std::string target{};
    };

    struct PackageOutputDefinition
    {
        std::string name{};
        std::string version{};
        std::string from{};
        std::string description{};
        std::string license{};
        bool disabled{false};
        std::vector<std::string> headers{};
        std::vector<std::string> libraries{};
        std::vector<std::string> tools{};
        std::vector<std::string> capabilities{};
        std::vector<std::string> targetPlatforms{};
        std::string abiTag{};
    };

    struct PublishDefinition
    {
        std::string name{};
        std::string kind{"Folder"};
        std::string format{};
        std::string output{};
        bool disabled{false};
        bool includeStage{true};
        bool includeRuntimeDependencies{false};
        bool includeSymbols{true};
    };

    struct LaunchDefinition
    {
        std::string name{};
        std::optional<std::string> executable{};
        std::string workingDirectory{"."};
        std::string args{};
        bool disabled{false};
    };

    struct EnvironmentDefinition
    {
        std::string name{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<PackageFeatureUse> packageFeatureUses{};
        std::vector<GeneratorDeclaration> generators{};
        std::vector<InputDeclaration> inputs{};
        std::vector<EnvironmentVariable> variables{};
        std::vector<FeatureFlag> features{};
        RuntimeDefinition runtime{};
    };

    struct AnalyzerDefinition
    {
        std::string name{};
        std::string scope{"Build"};
        bool enabled{true};
        std::string severity{"Warning"};
        std::string configPath{};
        SelectorSet selectors{};
    };

    struct QualityDefinition
    {
        std::vector<AnalyzerDefinition> analyzers{};
    };

    struct ProfileDefinition
    {
        std::string name{};
        std::string buildType{"Debug"};
        std::string hostPlatform{"host"};
        std::string platform{"linux-x64"};
        std::string toolchain{};
        std::string operatingSystem{"linux"};
        std::string architecture{"x64"};
        bool enableReflection{false};
        std::string environmentName{};
        LaunchDefinition launch{};
        std::vector<LaunchDefinition> launches{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<PackageFeatureUse> packageFeatureUses{};
        std::vector<GeneratorDeclaration> generators{};
        std::vector<InputDeclaration> inputs{};
        RuntimeDefinition runtime{};
        std::vector<PackageOutputDefinition> packageOutputs{};
        std::vector<PublishDefinition> publishes{};
        QualityDefinition quality{};
    };

    struct ProjectManifest
    {
        fs::path path{};
        std::string name{};
        std::string type{};
        std::string productKind{};
        std::string defaultProfile{};
        std::vector<InputDeclaration> inputs{};
        std::vector<ConditionDefinition> conditions{};
        std::unordered_map<std::string, std::string> dependencyVersions{};
        std::string versionResolution{"HighestCompatible"};
        std::string defaultFeatures{"Explicit"};
        std::string lockFile{"Optional"};
        OutputDefinition output{};
        bool hasExplicitProfiles{false};
        std::vector<PackageOutputDefinition> packageOutputs{};
        std::vector<PublishDefinition> publishes{};
        ProjectBuildDescriptor build{};
        std::vector<GeneratorDeclaration> generators{};
        std::vector<ProjectReference> projectRefs{};
        std::vector<PackageReference> packageRefs{};
        std::vector<PackageFeatureUse> packageFeatureUses{};
        std::vector<LocalSettingsImport> localSettingsImports{};
        RuntimeDefinition runtime{};
        std::vector<LaunchDefinition> launches{};
        QualityDefinition quality{};
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

    struct SelectedPackageFeature
    {
        std::string packageName{};
        std::string packageVersion{};
        fs::path manifestPath{};
        fs::path providerRoot{};
        std::string featureName{};
        std::string description{};
        SelectorSet selectors{};
        std::vector<CapabilityProvision> provides{};
        std::vector<CapabilityRequirement> requiredCapabilities{};
        std::vector<PackageReference> packageRefs{};
        std::vector<InputDeclaration> inputs{};
        ProjectBuildDescriptor build{};
        RuntimeDefinition runtime{};
        std::vector<EnvironmentVariable> variables{};
        std::vector<GeneratorDeclaration> generators{};
    };

    struct ResolvedGenerator
    {
        GeneratorDeclaration declaration{};
        std::string ownerKind{};
        std::string ownerName{};
        fs::path ownerDirectory{};
        fs::path manifestPath{};
        std::vector<ConditionDefinition> conditions{};
        std::string packageName{};
        fs::path packageDirectory{};
        fs::path providerRoot{};
    };

    struct ResolvedCapabilityProvider
    {
        std::string capability{};
        std::string packageName{};
        std::string featureName{};
        bool exclusive{false};
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
        std::vector<SelectedPackageFeature> selectedPackageFeatures{};
        std::vector<ResolvedGenerator> generators{};
        std::vector<ResolvedCapabilityProvider> capabilityProviders{};
        std::vector<ResolvedBootstrap> bootstraps{};
        std::vector<ResolvedPackage> orderedPackages{};
        std::map<std::string, std::string> packageScopes{};
        std::map<std::string, std::set<std::string>> packageEdges{};
        std::vector<std::string> requiredModules{};
        std::vector<std::string> optionalModules{};
        std::map<std::string, std::set<std::string>> dependencyEdges{};
        std::vector<std::string> enabledPlugins{};
        std::string targetAbiTag{};
        std::optional<WorkspaceManifest::Toolchain> selectedToolchain{};
        std::vector<LibraryArtifact> libraries{};
        std::vector<ExecutableArtifact> executables{};
        std::optional<ExecutableArtifact> selectedExecutable{};
    };

    struct CompositionGraph
    {
        struct Identity
        {
            std::string project{};
            fs::path projectPath{};
            std::string product{};
            std::string profile{};
        };

        struct Product
        {
            std::string kind{};
            std::string outputType{};
            std::string outputName{};
            std::string targetName{};
        };

        struct Selection
        {
            std::string profile{};
            std::string hostPlatform{};
            std::string targetPlatform{};
            std::string operatingSystem{};
            std::string architecture{};
            std::string toolchain{};
            std::string environment{};
            std::string abiTag{};
        };

        struct Provenance
        {
            std::string sourceKind{};
            std::string sourceName{};
            fs::path manifestPath{};
            std::string reason{};
        };

        struct Convention
        {
            std::string name{};
            std::string reason{};
            Provenance provenance{};
        };

        struct Property
        {
            std::string name{};
            std::string value{};
            Provenance provenance{};
        };

        struct Summary
        {
            std::size_t packages{};
            std::size_t packageFeatures{};
            std::size_t sources{};
            std::size_t headers{};
            std::size_t generators{};
            std::size_t stagedFiles{};
            std::size_t runtimeModules{};
            std::size_t environmentVariables{};
            std::size_t publishes{};
            std::size_t analyzers{};
            std::size_t diagnostics{};
        };

        struct StageFile
        {
            std::string kind{};
            fs::path source{};
            fs::path target{};
            std::string owner{};
            Provenance provenance{};
        };

        struct EnvironmentEntry
        {
            std::string name{};
            std::string value{};
            bool secret{};
            bool resolved{};
            std::string source{};
            Provenance provenance{};
        };

        struct PackageOutput
        {
            std::string name{};
            std::string version{};
            std::string from{};
            std::size_t headers{};
            std::size_t libraries{};
            std::size_t tools{};
            std::size_t capabilities{};
            std::string abi{};
            Provenance provenance{};
        };

        struct Package
        {
            std::string name{};
            std::string version{};
            std::string source{};
            fs::path providerRoot{};
            std::string scope{};
            std::vector<std::string> closures{};
            std::vector<std::string> dependencies{};
            Provenance provenance{};
        };

        struct PackageFeature
        {
            std::string package{};
            std::string feature{};
            std::string packageVersion{};
            Provenance provenance{};
        };

        struct BuildDefine
        {
            std::string value{};
            Provenance provenance{};
        };

        struct BuildInput
        {
            std::string kind{};
            std::string role{};
            std::string source{};
            std::string owner{};
            Provenance provenance{};
        };

        struct Generator
        {
            std::string name{};
            std::string owner{};
            std::string tool{};
            std::size_t outputs{};
            Provenance provenance{};
        };

        struct RuntimeModule
        {
            std::string name{};
            std::string selection{};
            Provenance provenance{};
        };

        struct RuntimePlugin
        {
            std::string name{};
            Provenance provenance{};
        };

        struct Launch
        {
            std::string name{};
            std::string executable{};
            std::string workingDirectory{};
            std::string args{};
            bool selected{false};
            Provenance provenance{};
        };

        struct Publish
        {
            std::string name{};
            std::string kind{};
            std::string format{};
            std::string output{};
            bool includeStage{};
            bool includeRuntimeDependencies{};
            bool includeSymbols{};
            Provenance provenance{};
        };

        struct QualityAnalyzer
        {
            std::string name{};
            std::string scope{};
            std::string severity{};
            std::string configPath{};
            Provenance provenance{};
        };

        std::string schemaVersion{"4.0"};
        std::string kind{"NGIN.CompositionGraph"};
        std::string state{"resolved"};
        std::vector<std::string> facets{};
        Identity identity{};
        Product product{};
        Selection selection{};
        std::vector<Convention> conventions{};
        std::vector<Property> properties{};
        Summary summary{};
        std::vector<StageFile> stageFiles{};
        std::vector<EnvironmentEntry> environment{};
        std::vector<PackageOutput> packageOutputs{};
        std::vector<Package> packages{};
        std::vector<PackageFeature> packageFeatures{};
        std::vector<BuildDefine> buildDefines{};
        std::vector<BuildInput> buildInputs{};
        std::vector<Generator> generators{};
        std::vector<RuntimeModule> runtimeModules{};
        std::vector<RuntimePlugin> runtimePlugins{};
        Launch launch{};
        std::vector<Launch> launches{};
        std::vector<Publish> publishes{};
        std::vector<QualityAnalyzer> analyzers{};
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
