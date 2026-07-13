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

    struct ToolingResolutionPolicy
    {
        bool allowPath{true};
        bool requireVersion{false};
        bool requireTrustedPackage{false};
        bool allowPathExplicit{false};
        bool requireVersionExplicit{false};
        bool requireTrustedPackageExplicit{false};
    };

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

        struct PackageProvider
        {
            std::string name{};
            std::string kind{};
            fs::path root{};
            std::string triplet{};
            std::string profile{};
        };

        struct ProfilePolicy
        {
            struct BuildSettingPolicy
            {
                std::string productKind{};
                std::string kind{};
                std::string value{};
                std::string visibility{"Private"};
                bool remove{false};
                std::string removeIdentity{};
            };

            struct ToolRunPolicy
            {
                struct ConfigPolicy
                {
                    std::string name{"primary"};
                    std::string path{};
                    bool optional{false};
                };

                struct ReportPolicy
                {
                    std::string name{};
                    std::string format{};
                    std::string path{};
                };

                struct SeverityPolicy
                {
                    std::string rule{};
                    std::string severity{};
                };

                struct SuppressionPolicy
                {
                    std::string rule{};
                    std::string fingerprint{};
                    std::string reason{};
                    std::string expires{};
                };

                struct RuleBudgetPolicy
                {
                    std::string rule{};
                    std::size_t maximum{};
                };

                std::string productKind{};
                std::string name{};
                std::string displayName{};
                std::string description{};
                std::string action{};
                bool enabled{true};
                bool remove{false};
                bool hasInput{false};
                std::string inputContract{};
                std::string inputScope{"Product"};
                std::string inputMerge{"Replace"};
                bool inputContractExplicit{false};
                bool inputScopeExplicit{false};
                bool includeGeneratedExplicit{false};
                bool includeGenerated{false};
                std::vector<std::string> includes{};
                std::vector<std::string> excludes{};
                std::vector<ConfigPolicy> configs{};
                bool hasPolicy{false};
                bool gate{false};
                bool gateExplicit{false};
                std::string failOn{"Error"};
                bool failOnExplicit{false};
                std::string baseline{};
                bool baselineExplicit{false};
                bool newFindingsOnly{false};
                bool newFindingsOnlyExplicit{false};
                std::optional<std::size_t> maxFindings{};
                bool maxFindingsExplicit{false};
                std::optional<std::size_t> maxWarnings{};
                bool maxWarningsExplicit{false};
                std::vector<SeverityPolicy> severityMappings{};
                std::vector<SuppressionPolicy> suppressions{};
                std::vector<RuleBudgetPolicy> ruleBudgets{};
                bool hasExecution{false};
                std::string jobs{"Auto"};
                bool jobsExplicit{false};
                std::string timeout{};
                bool timeoutExplicit{false};
                std::string cache{"Off"};
                bool cacheExplicit{false};
                std::string failureStrategy{"DependencyAware"};
                bool failureStrategyExplicit{false};
                std::size_t weight{1};
                bool weightExplicit{false};
                std::size_t maxParallelism{1};
                bool maxParallelismExplicit{false};
                std::string exclusiveResource{};
                bool exclusiveResourceExplicit{false};
                std::vector<std::string> dependencies{};
                std::vector<ReportPolicy> reports{};
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

            struct GeneratorPolicy
            {
                struct Argument
                {
                    std::string value{};
                    std::string path{};
                };

                struct Output
                {
                    std::string kind{};
                    std::string role{};
                    std::string path{};
                    std::string visibility{"Private"};
                };

                std::string productKind{};
                std::string name{};
                std::string toolName{};
                std::string toolExecutable{};
                std::vector<Argument> arguments{};
                std::vector<Output> outputs{};
                bool remove{false};
            };

            struct LaunchPolicy
            {
                std::string productKind{};
                std::string name{};
                std::optional<std::string> executable{};
                std::optional<std::string> workingDirectory{};
                std::optional<std::string> args{};
                bool remove{false};
            };

            struct PublishPolicy
            {
                std::string productKind{};
                std::string name{};
                std::string kind{"Folder"};
                std::string format{};
                std::string output{};
                bool includeStage{true};
                bool includeRuntimeDependencies{false};
                bool includeSymbols{true};
                bool remove{false};
            };

            struct PackageOutputPolicy
            {
                std::string productKind{};
                std::string name{};
                std::string version{};
                std::string from{};
                std::string description{};
                std::string license{};
                std::vector<std::string> headers{};
                std::vector<std::string> libraries{};
                std::vector<std::string> tools{};
                std::vector<std::string> capabilities{};
                std::vector<std::string> targetPlatforms{};
                std::string abiTag{};
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
            std::vector<ToolRunPolicy> toolRuns{};
            std::vector<EnvironmentVariablePolicy> environmentVariables{};
            std::vector<StageInputPolicy> stageInputs{};
            std::vector<DependencyUsePolicy> dependencyUses{};
            std::vector<RuntimeModulePolicy> runtimeModules{};
            std::vector<GeneratorPolicy> generators{};
            std::vector<LaunchPolicy> launches{};
            std::vector<PublishPolicy> publishes{};
            std::vector<PackageOutputPolicy> packageOutputs{};
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
        std::unordered_map<std::string, PackageProvider> externalPackageProviders{};
        std::unordered_map<std::string, std::string> dependencyVersions{};
        std::string versionResolution{"HighestCompatible"};
        std::string defaultFeatures{"Explicit"};
        std::string lockFile{"Optional"};
        ToolingResolutionPolicy toolingResolutionPolicy{};
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

    struct ContributionProvenance
    {
        std::string sourceKind{};
        std::string sourceName{};
        fs::path manifestPath{};
        std::string reason{};
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
        ContributionProvenance provenance{};
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
        ContributionProvenance provenance{};
        bool disabled{false};
        std::string removeIdentity{};
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
        ContributionProvenance provenance{};
        bool disabled{false};
        std::string removeIdentity{};
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
        ContributionProvenance provenance{};
        bool disabled{false};
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
        ContributionProvenance provenance{};
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
        std::string provider{};
        std::string providerPackage{};
        std::string providerVersion{};
        std::string cmakePackage{};
        std::string linkage{};
        std::string runtimeDeployment{};
        std::string runtimeArtifacts{};
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
        std::string overrideEnvironment{};
        std::string versionRange{};
        bool systemExecutable{false};
        SelectorSet selectors{};
    };

    struct ToolDriverDeclaration
    {
        std::string name{};
        std::string protocol{"NGIN.ToolDriver/1"};
        std::string executable{};
        std::string adapter{};
        std::string overrideEnvironment{};
        std::string version{};
        bool probe{false};
        std::vector<std::string> capabilities{};
        std::vector<std::string> probeArguments{};
        std::vector<std::string> arguments{};
        SelectorSet selectors{};
    };

    struct ToolActionDeclaration
    {
        struct EnvironmentRequirement
        {
            std::string name{};
            bool required{true};
            bool secret{false};
            bool cacheKey{false};
        };
        std::string name{};
        std::string kind{"Custom"};
        std::string toolName{};
        std::string driverName{};
        std::string toolVersionRange{};
        std::string driverVersionRange{};
        std::vector<std::string> inputContracts{};
        std::vector<std::string> capabilities{};
        std::vector<EnvironmentRequirement> environment{};
        std::string defaultInputScope{"Product"};
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
        ContributionProvenance provenance{};
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
        ContributionProvenance provenance{};
        bool disabled{false};
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
        ContributionProvenance provenance{};
    };

    struct RuntimeReference
    {
        std::string name{};
        SelectorSet selectors{};
        ContributionProvenance provenance{};
    };

    struct RuntimeDefinition
    {
        std::vector<ModuleDescriptor> modules{};
        std::vector<RuntimeReference> enableModules{};
        std::vector<RuntimeReference> disableModules{};
        std::vector<RuntimeReference> enablePlugins{};
        std::vector<RuntimeReference> disablePlugins{};
    };

    struct ToolConfigDefinition
    {
        std::string name{"primary"};
        std::string path{};
        bool optional{false};
    };

    struct ToolReportDefinition
    {
        std::string name{};
        std::string format{};
        std::string path{};
    };

    struct ToolInputDefinition
    {
        std::string contract{};
        std::string scope{"Product"};
        bool includeGenerated{false};
        std::string merge{"Replace"};
        bool contractExplicit{false};
        bool scopeExplicit{false};
        bool includeGeneratedExplicit{false};
        std::vector<std::string> includes{};
        std::vector<std::string> excludes{};
    };

    struct ToolPolicyDefinition
    {
        struct SeverityMapping
        {
            std::string rule{};
            std::string severity{};
        };

        struct Suppression
        {
            std::string rule{};
            std::string fingerprint{};
            std::string reason{};
            std::string expires{};
        };

        struct RuleBudget
        {
            std::string rule{};
            std::size_t maximum{};
        };

        bool gate{false};
        bool gateExplicit{false};
        std::string failOn{"Error"};
        bool failOnExplicit{false};
        std::string baseline{};
        bool baselineExplicit{false};
        bool newFindingsOnly{false};
        bool newFindingsOnlyExplicit{false};
        std::optional<std::size_t> maxFindings{};
        bool maxFindingsExplicit{false};
        std::optional<std::size_t> maxWarnings{};
        bool maxWarningsExplicit{false};
        std::vector<SeverityMapping> severityMappings{};
        std::vector<Suppression> suppressions{};
        std::vector<RuleBudget> ruleBudgets{};
    };

    struct ToolExecutionDefinition
    {
        std::string jobs{"Auto"};
        bool jobsExplicit{false};
        std::string timeout{};
        bool timeoutExplicit{false};
        std::string cache{"Off"};
        bool cacheExplicit{false};
        std::string failureStrategy{"DependencyAware"};
        bool failureStrategyExplicit{false};
        std::size_t weight{1};
        bool weightExplicit{false};
        std::size_t maxParallelism{1};
        bool maxParallelismExplicit{false};
        std::string exclusiveResource{};
        bool exclusiveResourceExplicit{false};
    };

    struct ToolRunDefinition
    {
        std::string name{};
        std::string displayName{};
        std::string description{};
        std::string action{};
        std::string packageName{};
        std::string packageFeature{};
        bool enabled{true};
        bool disabled{false};
        bool excluded{false};
        ToolInputDefinition input{};
        bool hasInput{false};
        std::vector<ToolConfigDefinition> configs{};
        ToolPolicyDefinition policy{};
        bool hasPolicy{false};
        ToolExecutionDefinition execution{};
        bool hasExecution{false};
        std::vector<std::string> dependencies{};
        std::vector<ToolReportDefinition> reports{};
        SelectorSet selectors{};
        ContributionProvenance provenance{};
        ContributionProvenance originProvenance{};
    };

    struct ToolingDefinition
    {
        std::vector<ToolRunDefinition> runs{};
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
        std::vector<ToolDriverDeclaration> toolDrivers{};
        std::vector<ToolActionDeclaration> toolActions{};
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
            ToolingDefinition tooling{};
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
        ContributionProvenance provenance{};
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
        ContributionProvenance provenance{};
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
        ContributionProvenance provenance{};
    };

    struct LaunchDefinition
    {
        std::string name{};
        std::optional<std::string> executable{};
        std::string workingDirectory{"."};
        std::string args{};
        bool disabled{false};
        ContributionProvenance provenance{};
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
        ToolingDefinition tooling{};
        ToolingResolutionPolicy toolingResolutionPolicy{};
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
        ToolingDefinition tooling{};
        ToolingResolutionPolicy toolingResolutionPolicy{};
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
        ToolingDefinition tooling{};
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
        std::map<std::string, ContributionProvenance> runtimeModuleProvenance{};
        std::map<std::string, std::set<std::string>> dependencyEdges{};
        std::vector<std::string> enabledPlugins{};
        std::string targetAbiTag{};
        std::optional<WorkspaceManifest::Toolchain> selectedToolchain{};
        ToolingResolutionPolicy toolingResolutionPolicy{};
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
            std::size_t toolRuns{};
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
            std::string provider{};
            std::string providerKind{};
            std::string providerPackage{};
            std::string providerVersion{};
            std::string linkage{};
            std::string runtimeDeployment{};
            std::string runtimeArtifacts{};
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

        struct ToolRun
        {
            std::string name{};
            std::string displayName{};
            std::string description{};
            std::string action{};
            std::string actionKind{};
            std::string packageName{};
            std::string packageFeature{};
            std::string tool{};
            std::string toolPath{};
            std::string toolSource{};
            std::string driver{};
            std::string driverPath{};
            std::string driverSource{};
            std::string driverProtocol{};
            std::vector<std::string> capabilities{};
            std::string state{"ready"};
            std::string diagnostic{};
            std::string inputContract{};
            std::string inputScope{};
            bool includeGenerated{};
            std::size_t configCount{};
            std::vector<std::string> configNames{};
            std::vector<std::string> configPaths{};
            std::vector<bool> configOptional{};
            std::vector<std::string> includes{};
            std::vector<std::string> excludes{};
            std::vector<std::string> inputFiles{};
            bool gate{};
            std::string failOn{};
            std::string baseline{};
            bool newFindingsOnly{};
            std::string cache{};
            std::string jobs{};
            std::string timeout{};
            std::string failureStrategy{};
            std::size_t weight{1};
            std::size_t maxParallelism{1};
            std::string exclusiveResource{};
            std::size_t reportCount{};
            std::vector<std::string> reportNames{};
            std::vector<std::string> reportPaths{};
            std::vector<std::string> reportFormats{};
            std::vector<std::string> dependencies{};
            Provenance provenance{};
            Provenance originProvenance{};
        };

        struct Tool
        {
            std::string identity{};
            std::string name{};
            std::string packageName{};
            std::string kind{};
            std::string executable{};
            std::string resolvedPath{};
            std::string resolutionSource{};
            std::string versionRange{};
            bool systemExecutable{};
            Provenance provenance{};
        };

        struct ToolDriver
        {
            std::string identity{};
            std::string name{};
            std::string packageName{};
            std::string protocol{};
            std::string version{};
            std::string executable{};
            std::string resolvedPath{};
            std::string resolutionSource{};
            bool probe{};
            std::vector<std::string> capabilities{};
            Provenance provenance{};
        };

        struct ToolAction
        {
            struct EnvironmentRequirement
            {
                std::string name{};
                bool required{};
                bool secret{};
                bool cacheKey{};
                bool resolved{};
            };
            std::string identity{};
            std::string name{};
            std::string packageName{};
            std::string kind{};
            std::string tool{};
            std::string driver{};
            std::vector<std::string> inputContracts{};
            std::vector<std::string> capabilities{};
            std::string defaultInputScope{};
            std::vector<EnvironmentRequirement> environment{};
            Provenance provenance{};
        };

        struct ToolInputSet
        {
            struct TranslationUnit
            {
                std::string source{};
                std::string workingDirectory{};
                std::string compiler{};
                std::vector<std::string> arguments{};
                std::string targetPlatform{};
                std::string language{};
                std::string owner{};
                bool generated{};
                std::string commandDigest{};
            };
            std::string identity{};
            std::string run{};
            std::string contract{};
            std::string scope{};
            std::string state{"resolved"};
            std::string source{};
            std::string signature{};
            bool includeGenerated{};
            std::vector<std::string> files{};
            std::vector<TranslationUnit> translationUnits{};
        };

        struct ToolPolicy
        {
            std::string identity{};
            std::string run{};
            bool gate{};
            std::string failOn{};
            std::string baseline{};
            bool newFindingsOnly{};
            std::optional<std::size_t> maxFindings{};
            std::optional<std::size_t> maxWarnings{};
        };

        struct ToolReport
        {
            std::string identity{};
            std::string run{};
            std::string name{};
            std::string format{};
            std::string path{};
        };

        struct ToolDependency
        {
            std::string from{};
            std::string to{};
            std::string kind{};
        };

        struct ToolPlanDiagnostic
        {
            std::string run{};
            std::string severity{};
            std::string message{};
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
        std::vector<Tool> tools{};
        std::vector<ToolDriver> toolDrivers{};
        std::vector<ToolAction> toolActions{};
        std::vector<ToolRun> toolRuns{};
        std::vector<ToolInputSet> toolInputSets{};
        std::vector<ToolPolicy> toolPolicies{};
        std::vector<ToolReport> toolReports{};
        std::vector<ToolDependency> toolDependencies{};
        std::vector<ToolPlanDiagnostic> toolDiagnostics{};
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
} // namespace NGIN::CLI
