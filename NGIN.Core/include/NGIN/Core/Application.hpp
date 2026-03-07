#pragma once

/// @file Application.hpp
/// @brief Public builder-first application host API for NGIN.Core.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Core/Config.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Core/HostConfig.hpp>
#include <NGIN/Core/Kernel.hpp>
#include <NGIN/Core/Loader.hpp>
#include <NGIN/Core/Services.hpp>
#include <NGIN/Utilities/Any.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Core
{
    enum class HostProfile : NGIN::UInt8
    {
        ConsoleApp,
        GuiApp,
        Game,
        Editor,
        Service,
        TestHost
    };

    enum class TargetType : NGIN::UInt8
    {
        Runtime,
        Editor,
        Program,
        Developer
    };

    struct PackageReference
    {
        std::string name {};
        std::string versionRange {};
        bool        optional {false};
    };

    enum class PackageBootstrapMode : NGIN::UInt8
    {
        BuilderHookV1
    };

    struct PackageBootstrapDescriptor
    {
        PackageBootstrapMode mode {PackageBootstrapMode::BuilderHookV1};
        std::string          entryPoint {};
        bool                 autoApply {false};
    };

    class PackageBootstrapContext;
    class PackageBootstrapRegistry;

    using PackageBootstrapFn = CoreResult<void> (*)(PackageBootstrapContext&);
    using PackageBootstrapRegistrarFn = void (*)(PackageBootstrapRegistry&);

    struct PackageBootstrapEntry
    {
        std::string        packageName {};
        std::string        entryPoint {};
        PackageBootstrapFn fn {nullptr};
    };

    struct PackageContentFile
    {
        std::string source {};
        std::string target {};
        std::string kind {};
    };

    struct PackagePluginManifest
    {
        std::string              name {};
        std::vector<std::string> platforms {};
        std::vector<std::string> requiredModules {};
        std::vector<std::string> optionalModules {};
        bool                     optional {false};
    };

    struct PackageManifest
    {
        NGIN::UInt32                             schemaVersion {1};
        std::string                              name {};
        std::string                              version {};
        std::string                              compatiblePlatformRange {};
        std::vector<std::string>                 platforms {};
        std::vector<PackageReference>            dependencies {};
        std::optional<PackageBootstrapDescriptor> bootstrap {};
        std::vector<PackageContentFile>          contents {};
        std::vector<ModuleDescriptor>            modules {};
        std::vector<PackagePluginManifest>       plugins {};
    };

    struct PluginReference
    {
        std::string name {};
        std::string versionRange {};
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
        std::vector<std::string> searchPaths {};
    };

    struct TargetDefinition
    {
        std::string                   name {};
        TargetType                    type {TargetType::Runtime};
        HostProfile                   profile {HostProfile::ConsoleApp};
        std::string                   platform {};
        bool                          enableReflection {false};
        std::vector<PackageReference> packages {};
        ModuleSelection               modules {};
        PluginSelection               plugins {};
        std::string                   environmentName {};
        std::vector<std::string>      configSources {};
        std::string                   workingDirectory {};
    };

    struct ProjectManifest
    {
        NGIN::UInt32                  schemaVersion {1};
        std::string                   name {};
        std::string                   defaultTarget {};
        std::vector<TargetDefinition> targets {};
    };

    class ServiceCollection
    {
    public:
        virtual ~ServiceCollection() = default;

        virtual auto AddSingleton(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceMetadata metadata = {}) -> ServiceCollection& = 0;

        virtual auto AddFactory(
            std::string key,
            ServiceFactory factory,
            ServiceLifetime lifetime,
            ServiceMetadata metadata = {}) -> ServiceCollection& = 0;

        virtual auto AddScoped(
            std::string key,
            ServiceFactory factory,
            ServiceMetadata metadata = {}) -> ServiceCollection& = 0;

        virtual auto AddTransient(
            std::string key,
            ServiceFactory factory,
            ServiceMetadata metadata = {}) -> ServiceCollection& = 0;

        virtual auto AddDefaults() -> ServiceCollection& = 0;
        virtual auto AddLogging() -> ServiceCollection& = 0;
        virtual auto AddConfiguration() -> ServiceCollection& = 0;
        virtual auto Clear() -> ServiceCollection& = 0;
    };

    class PackageCollection
    {
    public:
        virtual ~PackageCollection() = default;

        virtual auto Add(PackageReference reference) -> PackageCollection& = 0;
        virtual auto AddManifest(PackageManifest manifest) -> PackageCollection& = 0;
        virtual auto AddManifestFile(std::string path) -> PackageCollection& = 0;
        virtual auto RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar) -> PackageCollection& = 0;
        virtual auto ApplyBootstrap(std::string packageName) -> PackageCollection& = 0;
        virtual auto ApplyBootstrap(std::string packageName, std::string entryPoint) -> PackageCollection& = 0;
        virtual auto Clear() -> PackageCollection& = 0;
    };

    class ModuleCollection
    {
    public:
        virtual ~ModuleCollection() = default;

        virtual auto Register(StaticModuleRegistration registration) -> ModuleCollection& = 0;
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
        virtual auto Clear() -> ConfigurationBuilder& = 0;
    };

    class PackageBootstrapContext
    {
    public:
        virtual ~PackageBootstrapContext() = default;

        [[nodiscard]] virtual auto PackageName() const noexcept -> std::string_view = 0;
        [[nodiscard]] virtual auto TargetName() const noexcept -> std::string_view = 0;
        [[nodiscard]] virtual auto Profile() const noexcept -> HostProfile = 0;

        [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection& = 0;
        [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection& = 0;
        [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection& = 0;
        [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection& = 0;
        [[nodiscard]] virtual auto Configuration() noexcept -> ConfigurationBuilder& = 0;
    };

    class PackageBootstrapRegistry
    {
    public:
        virtual ~PackageBootstrapRegistry() = default;

        virtual auto Register(PackageBootstrapEntry entry) -> CoreResult<void> = 0;
        [[nodiscard]] virtual auto Find(std::string_view packageName, std::string_view entryPoint) const noexcept
            -> const PackageBootstrapEntry* = 0;
        [[nodiscard]] virtual auto FindDefault(std::string_view packageName) const noexcept
            -> const PackageBootstrapEntry* = 0;
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

        [[nodiscard]] virtual auto GetServices() noexcept -> NGIN::Memory::Shared<IServiceRegistry> = 0;
        [[nodiscard]] virtual auto GetConfig() noexcept -> NGIN::Memory::Shared<IConfigStore> = 0;
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

    [[nodiscard]] NGIN_CORE_API auto CreateApplicationBuilder(int argc, char** argv) -> std::unique_ptr<ApplicationBuilder>;
}
