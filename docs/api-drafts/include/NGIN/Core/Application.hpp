#pragma once

/// @file Application.hpp
/// @brief Draft-only public API surface for the first NGIN.Core application model.
///
/// This header is documentation-first and is not wired into a build target yet.

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::Core
{
    class ServiceCollection;
    class PackageCollection;
    class ModuleCollection;
    class PluginCollection;
    class ConfigurationBuilder;
    class IServiceProvider;
    class IConfigurationRoot;

    enum class HostProfile
    {
        ConsoleApp,
        GuiApp,
        Game,
        Editor,
        Service,
        TestHost
    };

    enum class TargetType
    {
        Runtime,
        Editor,
        Program,
        Developer
    };

    enum class ServiceLifetime
    {
        Singleton,
        Scoped,
        Transient
    };

    enum class ServiceScopeKind
    {
        Host,
        Package,
        Module,
        Operation
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

    struct StartupWarning
    {
        std::string subsystem {};
        std::string subject {};
        std::string message {};
    };

    struct StartupReport
    {
        std::string              targetName {};
        HostProfile              profile {HostProfile::ConsoleApp};
        std::vector<std::string> resolvedPackages {};
        std::vector<std::string> resolvedModules {};
        std::vector<std::string> resolvedPlugins {};
        std::vector<StartupWarning> warnings {};
    };

    struct PackageReference
    {
        std::string name {};
        std::string versionRange {};
        bool        optional {false};
    };

    struct ServiceRegistration
    {
        std::string     serviceKey {};
        std::string     implementationKey {};
        ServiceLifetime lifetime {ServiceLifetime::Singleton};
        bool            tryAdd {false};
    };

    struct PluginReference
    {
        std::string name {};
        std::string versionRange {};
    };

    struct ServiceScope
    {
        std::string      id {};
        ServiceScopeKind kind {ServiceScopeKind::Host};
        std::string      owner {};
    };

    struct PackageBootstrapDescriptor
    {
        PackageBootstrapMode mode {PackageBootstrapMode::BuilderHookV1};
        std::string          entryPoint {};
        bool                 autoApply {false};
    };

    struct ModuleSelection
    {
        std::vector<std::string> enable {};
        std::vector<std::string> disable {};
    };

    struct PluginSelection
    {
        std::vector<std::string> enable {};
        std::vector<std::string> disable {};
    };

    struct TargetDefinition
    {
        std::string              name {};
        TargetType               type {TargetType::Runtime};
        HostProfile              profile {HostProfile::ConsoleApp};
        std::string              platform {};
        bool                     enableReflection {false};
        std::vector<PackageReference> packages {};
        ModuleSelection          modules {};
        PluginSelection          plugins {};
        std::string              environmentName {};
        std::vector<std::string> configSources {};
        std::string              workingDirectory {};
    };

    struct ProjectManifest
    {
        int                      schemaVersion {1};
        std::string              name {};
        std::string              defaultTarget {};
        std::vector<TargetDefinition> targets {};
    };

    struct PackageManifest
    {
        struct ProvidedContent
        {
            std::vector<std::string> modules {};
            std::vector<std::string> plugins {};
        };

        int                      schemaVersion {1};
        std::string              name {};
        std::string              version {};
        std::string              compatiblePlatformRange {};
        std::vector<std::string> platforms {};
        std::vector<PackageReference> dependencies {};
        std::optional<PackageBootstrapDescriptor> bootstrap {};
        ProvidedContent          provides {};
    };

    class ServiceCollection
    {
    public:
        virtual ~ServiceCollection() = default;

        virtual auto Add(ServiceRegistration registration) -> ServiceCollection& = 0;
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
        virtual auto AddSearchPath(std::string path) -> PluginCollection& = 0;
        virtual auto Clear() -> PluginCollection& = 0;
    };

    class ConfigurationBuilder
    {
    public:
        virtual ~ConfigurationBuilder() = default;

        virtual auto AddSource(std::string path) -> ConfigurationBuilder& = 0;
        virtual auto SetEnvironmentName(std::string environmentName) -> ConfigurationBuilder& = 0;
        virtual auto SetWorkingDirectory(std::string workingDirectory) -> ConfigurationBuilder& = 0;
    };

    class PackageBootstrapContext
    {
    public:
        virtual ~PackageBootstrapContext() = default;

        [[nodiscard]] virtual auto PackageName() const -> std::string = 0;
        [[nodiscard]] virtual auto TargetName() const -> std::string = 0;
        [[nodiscard]] virtual auto Profile() const noexcept -> HostProfile = 0;

        [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection& = 0;
        [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection& = 0;
        [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection& = 0;
        [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection& = 0;
        [[nodiscard]] virtual auto Configuration() noexcept -> ConfigurationBuilder& = 0;
    };

    using PackageBootstrapFn = CoreResult<void>(*)(PackageBootstrapContext&);

    class IServiceProvider
    {
    public:
        virtual ~IServiceProvider() = default;

        virtual auto BeginScope(ServiceScopeKind kind, std::string owner) -> CoreResult<ServiceScope> = 0;
        virtual auto EndScope(std::string scopeId) -> CoreResult<void> = 0;
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

        [[nodiscard]] virtual auto GetProfile() const noexcept -> HostProfile = 0;
        [[nodiscard]] virtual auto GetTargetName() const -> std::string = 0;
        [[nodiscard]] virtual auto GetStartupReport() const -> StartupReport = 0;

        [[nodiscard]] virtual auto GetServices() noexcept -> IServiceProvider& = 0;
        [[nodiscard]] virtual auto GetConfig() noexcept -> IConfigurationRoot& = 0;
    };

    class ApplicationBuilder
    {
    public:
        virtual ~ApplicationBuilder() = default;

        virtual auto UseProjectFile(std::string path) -> ApplicationBuilder& = 0;
        virtual auto UseProject(ProjectManifest manifest) -> ApplicationBuilder& = 0;
        virtual auto SetApplicationName(std::string applicationName) -> ApplicationBuilder& = 0;
        virtual auto UseProfile(HostProfile profile) -> ApplicationBuilder& = 0;
        virtual auto SetDefaultTarget(std::string targetName) -> ApplicationBuilder& = 0;

        [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection& = 0;
        [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection& = 0;
        [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection& = 0;
        [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection& = 0;
        [[nodiscard]] virtual auto Configuration() noexcept -> ConfigurationBuilder& = 0;

        [[nodiscard]] virtual auto Build() -> CoreResult<std::shared_ptr<IApplicationHost>> = 0;
    };

    [[nodiscard]] auto CreateApplicationBuilder(int argc, char** argv) -> std::unique_ptr<ApplicationBuilder>;
}
