#pragma once

/// @file Application.hpp
/// @brief Draft-only public API surface for the NGIN.Core application model.
///
/// This header is documentation-first and is not wired into a build target yet.

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::Core
{
    class PackageBootstrapContext;
    class ServiceCollection;
    class PackageCollection;
    class ModuleCollection;
    class PluginCollection;
    class ConfigurationBuilder;
    class IServiceProvider;
    class IConfigurationRoot;

    enum class ServiceLifetime
    {
        Singleton,
        Scoped,
        Transient
    };

    enum class PackageBootstrapMode
    {
        BuilderHookV1
    };

    struct HostError
    {
        std::string code {};
        std::string message {};
        std::string detail {};
    };

    template <typename T>
    using CoreResult = std::expected<T, HostError>;

    using PackageBootstrapFn = CoreResult<void>(*)(PackageBootstrapContext&);

    struct StartupWarning
    {
        std::string subsystem {};
        std::string subject {};
        std::string message {};
    };

    struct StartupReport
    {
        std::string                 configurationName {};
        std::string                 hostType {};
        std::vector<std::string>    resolvedPackages {};
        std::vector<std::string>    resolvedModules {};
        std::vector<std::string>    resolvedPlugins {};
        std::vector<StartupWarning> warnings {};
    };

    struct PackageReference
    {
        std::string name {};
        std::string versionRange {};
        bool        optional {false};
    };

    struct PackageBootstrapDescriptor
    {
        PackageBootstrapMode mode {PackageBootstrapMode::BuilderHookV1};
        std::string          entryPoint {};
        bool                 autoApply {false};
    };

    struct PackageBootstrapEntry
    {
        std::string        packageName {};
        std::string        entryPoint {};
        PackageBootstrapFn fn {nullptr};
    };

    struct ProjectReference
    {
        std::string                path {};
        std::optional<std::string> configuration {};
    };

    struct OutputDefinition
    {
        std::string kind {};
        std::string name {};
        std::string target {};
    };

    struct BuildSetting
    {
        std::string value {};
        std::string visibility {"Private"};
    };

    struct ProjectBuildDescriptor
    {
        std::string              backend {"CMake"};
        std::string              mode {"Generated"};
        std::string              language {"CXX"};
        std::string              languageStandard {"23"};
        std::vector<std::string> sources {};
        std::vector<BuildSetting> includeDirectories {};
        std::vector<BuildSetting> compileDefinitions {};
        std::vector<BuildSetting> compileOptions {};
        std::vector<BuildSetting> linkOptions {};
    };

    struct LaunchDefinition
    {
        std::string executable {};
        std::string workingDirectory {"."};
    };

    struct RuntimeDefinition
    {
        std::vector<std::string> modules {};
        std::vector<std::string> enableModules {};
        std::vector<std::string> disableModules {};
    };

    struct EnvironmentVariable
    {
        std::string name {};
        std::string value {};
    };

    struct FeatureFlag
    {
        std::string name {};
        bool        enabled {false};
    };

    struct EnvironmentDefinition
    {
        std::string                    name {};
        std::vector<ProjectReference>  projectRefs {};
        std::vector<PackageReference>  packageRefs {};
        std::vector<std::string>       configSources {};
        std::vector<EnvironmentVariable> variables {};
        std::vector<FeatureFlag>       features {};
        RuntimeDefinition              runtime {};
    };

    struct ConfigurationDefinition
    {
        std::string                   name {};
        std::string                   buildConfiguration {"Debug"};
        std::string                   operatingSystem {"linux"};
        std::string                   architecture {"x64"};
        bool                          enableReflection {false};
        std::vector<PackageReference> packageRefs {};
        std::string                   environmentName {};
        std::vector<std::string>      configSources {};
        std::optional<LaunchDefinition> launch {};
        RuntimeDefinition             runtime {};
    };

    struct ProjectManifest
    {
        int                             schemaVersion {2};
        std::string                     name {};
        std::string                     type {};
        std::string                     defaultConfiguration {};
        std::vector<std::string>        sourceRoots {};
        OutputDefinition                primaryOutput {};
        ProjectBuildDescriptor          build {};
        std::vector<ProjectReference>   projectRefs {};
        std::vector<PackageReference>   packageRefs {};
        std::vector<std::string>        configSources {};
        std::vector<EnvironmentDefinition> environments {};
        RuntimeDefinition               runtime {};
        std::vector<ConfigurationDefinition> configurations {};
    };

    class PackageBootstrapRegistry
    {
    public:
        virtual ~PackageBootstrapRegistry() = default;

        virtual auto Register(PackageBootstrapEntry entry) -> CoreResult<void> = 0;
        [[nodiscard]] virtual auto Find(std::string packageName, std::string entryPoint) const
            -> std::optional<PackageBootstrapEntry> = 0;
        [[nodiscard]] virtual auto FindDefault(std::string packageName) const
            -> std::optional<PackageBootstrapEntry> = 0;
    };

    using PackageBootstrapRegistrarFn = void(*)(PackageBootstrapRegistry&);

    class ServiceCollection
    {
    public:
        virtual ~ServiceCollection() = default;

        virtual auto AddSingleton(std::string serviceKey, std::string implementationKey = {}) -> ServiceCollection& = 0;
        virtual auto AddScoped(std::string serviceKey, std::string implementationKey = {}) -> ServiceCollection& = 0;
        virtual auto AddTransient(std::string serviceKey, std::string implementationKey = {}) -> ServiceCollection& = 0;
        virtual auto AddDefaults() -> ServiceCollection& = 0;
        virtual auto AddLogging() -> ServiceCollection& = 0;
        virtual auto AddConfiguration() -> ServiceCollection& = 0;
    };

    class PackageCollection
    {
    public:
        virtual ~PackageCollection() = default;

        virtual auto Add(PackageReference reference) -> PackageCollection& = 0;
        virtual auto RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar) -> PackageCollection& = 0;
        virtual auto ApplyBootstrap(std::string packageName) -> PackageCollection& = 0;
        virtual auto ApplyBootstrap(std::string packageName, std::string entryPoint) -> PackageCollection& = 0;
        virtual auto Clear() -> PackageCollection& = 0;
    };

    class ModuleCollection
    {
    public:
        virtual ~ModuleCollection() = default;

        virtual auto Enable(std::string moduleName) -> ModuleCollection& = 0;
        virtual auto Disable(std::string moduleName) -> ModuleCollection& = 0;
        virtual auto Clear() -> ModuleCollection& = 0;
    };

    class PluginCollection
    {
    public:
        virtual ~PluginCollection() = default;

        virtual auto Enable(std::string pluginName) -> PluginCollection& = 0;
        virtual auto Disable(std::string pluginName) -> PluginCollection& = 0;
        virtual auto Clear() -> PluginCollection& = 0;
    };

    class ConfigurationBuilder
    {
    public:
        virtual ~ConfigurationBuilder() = default;

        virtual auto AddSource(std::string path) -> ConfigurationBuilder& = 0;
        virtual auto SetEnvironmentName(std::string environmentName) -> ConfigurationBuilder& = 0;
        virtual auto SetWorkingDirectory(std::string workingDirectory) -> ConfigurationBuilder& = 0;
        virtual auto Clear() -> ConfigurationBuilder& = 0;
    };

    class PackageBootstrapContext
    {
    public:
        virtual ~PackageBootstrapContext() = default;

        [[nodiscard]] virtual auto PackageName() const noexcept -> std::string_view = 0;
        [[nodiscard]] virtual auto ConfigurationName() const noexcept -> std::string_view = 0;

        [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection& = 0;
        [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection& = 0;
        [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection& = 0;
        [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection& = 0;
        [[nodiscard]] virtual auto Configuration() noexcept -> ConfigurationBuilder& = 0;
    };

    class IApplicationHost
    {
    public:
        virtual ~IApplicationHost() = default;

        virtual auto Start() noexcept -> CoreResult<void> = 0;
        virtual auto Run() noexcept -> CoreResult<void> = 0;
        virtual auto Tick() noexcept -> CoreResult<void> = 0;
        virtual void RequestStop(std::string reason) noexcept = 0;
        virtual auto Shutdown() noexcept -> CoreResult<void> = 0;

        [[nodiscard]] virtual auto GetConfigurationName() const -> std::string = 0;
        [[nodiscard]] virtual auto GetStartupReport() const -> StartupReport = 0;
    };

    class ApplicationBuilder
    {
    public:
        virtual ~ApplicationBuilder() = default;

        virtual auto UseProjectFile(std::string path) -> ApplicationBuilder& = 0;
        virtual auto UseProject(ProjectManifest manifest) -> ApplicationBuilder& = 0;
        virtual auto SetApplicationName(std::string applicationName) -> ApplicationBuilder& = 0;
        virtual auto SetConfiguration(std::string configurationName) -> ApplicationBuilder& = 0;

        [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection& = 0;
        [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection& = 0;
        [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection& = 0;
        [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection& = 0;
        [[nodiscard]] virtual auto Configuration() noexcept -> ConfigurationBuilder& = 0;

        [[nodiscard]] virtual auto Build() -> CoreResult<std::shared_ptr<IApplicationHost>> = 0;
    };
}
