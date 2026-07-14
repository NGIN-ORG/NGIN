#pragma once

/// @file Application.hpp
/// @brief Public builder-first application host API for NGIN.Core.

#include <NGIN/Core/Config.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Core/HostConfig.hpp>
#include <NGIN/Core/Kernel.hpp>
#include <NGIN/Core/Loader.hpp>
#include <NGIN/Core/Services.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NGIN::IO {
class IFileSystem;
}

namespace NGIN::Core {
struct PackageReference {
  std::string name{};
  std::string versionRange{};
  bool optional{false};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

enum class PackageBootstrapMode : NGIN::UInt8 { BuilderHook };

struct PackageBootstrapDescriptor {
  PackageBootstrapMode mode{PackageBootstrapMode::BuilderHook};
  std::string entryPoint{};
  bool autoApply{false};
};

class PackageBootstrapContext;
class PackageBootstrapRegistry;

using PackageBootstrapFn = CoreResult<void> (*)(PackageBootstrapContext &);
using PackageBootstrapRegistrarFn = void (*)(PackageBootstrapRegistry &);

struct PackageBootstrapEntry {
  std::string packageName{};
  std::string entryPoint{};
  PackageBootstrapFn fn{nullptr};
};

struct InputMetadataProperty {
  std::string name{};
  std::string value{};
};

struct InputDeclaration {
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
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
  std::vector<std::string> includePatterns{};
  std::vector<std::string> excludePatterns{};
  std::string setName{};
  std::string declaringScope{};
  std::vector<InputMetadataProperty> metadata{};
};

struct LaunchDefinition {
  std::string executable{};
  std::string workingDirectory{"."};
};

struct EnvironmentVariable {
  std::string name{};
  std::string value{};
};

struct FeatureFlag {
  std::string name{};
  bool enabled{false};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

struct PackageFeatureUse {
  std::string packageName{};
  std::string featureName{};
  std::string versionRange{};
  bool disabled{false};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

struct CapabilityRequirement {
  std::string name{};
};

struct CapabilityProvision {
  std::string name{};
  bool exclusive{false};
};

struct ToolDeclaration {
  std::string name{};
  std::string kind{"Generator"};
  std::string executable{};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

struct GeneratorArgument {
  std::string value{};
  std::string path{};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

struct GeneratorDeclaration {
  std::string name{};
  std::string kind{};
  std::string packageName{};
  std::string toolName{};
  std::optional<ToolDeclaration> inlineTool{};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
  std::vector<GeneratorArgument> arguments{};
  std::vector<InputDeclaration> inputs{};
  std::vector<InputDeclaration> outputs{};
};

struct PackagePluginManifest {
  std::string name{};
  std::vector<std::string> operatingSystems{};
  std::vector<std::string> architectures{};
  std::vector<std::string> requiredModules{};
  std::vector<std::string> optionalModules{};
  bool optional{false};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

struct ConditionNode {
  std::string kind{"Match"};
  std::string profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string conditionName{};
  std::vector<ConditionNode> children{};
};

struct ConditionDefinition {
  std::string name{};
  ConditionNode body{};
};

struct RuntimeDefinition {
  std::vector<ModuleDescriptor> modules{};
  std::vector<std::string> enableModules{};
  std::vector<std::string> disableModules{};
};

struct PackageManifest {
  NGIN::UInt32 schemaVersion{4};
  std::string path{};
  std::string directory{};
  std::string name{};
  std::string version{};
  std::string compatiblePlatformRange{};
  std::vector<std::string> operatingSystems{};
  std::vector<std::string> architectures{};
  std::vector<PackageReference> dependencies{};
  std::optional<PackageBootstrapDescriptor> bootstrap{};
  std::vector<InputDeclaration> inputs{};
  std::vector<ConditionDefinition> conditions{};
  std::vector<ModuleDescriptor> modules{};
  std::vector<PackagePluginManifest> plugins{};
  std::vector<ToolDeclaration> tools{};
  struct Feature {
    std::string name{};
    std::string description{};
    std::string profile{};
    std::string platform{};
    std::string operatingSystem{};
    std::string architecture{};
    std::string toolchain{};
    std::string environment{};
    std::string condition{};
    std::vector<CapabilityProvision> provides{};
    std::vector<CapabilityRequirement> requiredCapabilities{};
    std::vector<PackageReference> dependencies{};
    std::vector<InputDeclaration> inputs{};
    RuntimeDefinition runtime{};
    std::vector<EnvironmentVariable> variables{};
    std::vector<GeneratorDeclaration> generators{};
  };
  std::vector<Feature> features{};
};

struct PluginReference {
  std::string name{};
  std::string versionRange{};
};

struct ModuleSelection {
  std::vector<std::string> enable{};
  std::vector<std::string> disable{};
};

struct ProjectReference {
  std::string path{};
  std::optional<std::string> profile{};
  std::string platform{};
  std::string operatingSystem{};
  std::string architecture{};
  std::string toolchain{};
  std::string environment{};
  std::string condition{};
};

struct OutputDefinition {
  std::string kind{};
  std::string name{};
  std::string target{};
};

struct BuildSetting {
  std::string value{};
  std::string visibility{"Private"};
};

struct ProjectBuildDescriptor {
  std::string backend{"CMake"};
  std::string mode{"Generated"};
  std::string language{"CXX"};
  std::string languageStandard{"23"};
  std::vector<std::string> sources{};
  std::vector<BuildSetting> includeDirectories{};
  std::vector<BuildSetting> compileDefinitions{};
  std::vector<BuildSetting> compileOptions{};
  std::vector<BuildSetting> linkOptions{};
};

struct EnvironmentDefinition {
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

struct ProfileDefinition {
  std::string name{};
  std::string optimization{"Off"};
  bool debugSymbols{true};
  bool linkTimeOptimization{false};
  std::string toolchain{};
  std::string platform{"linux-x64"};
  std::string operatingSystem{"linux"};
  std::string architecture{"x64"};
  bool enableReflection{false};
  std::string environmentName{};
  std::vector<ProjectReference> projectRefs{};
  std::vector<PackageReference> packageRefs{};
  std::vector<PackageFeatureUse> packageFeatureUses{};
  std::vector<GeneratorDeclaration> generators{};
  std::vector<InputDeclaration> inputs{};
  std::optional<LaunchDefinition> launch{};
  RuntimeDefinition runtime{};
};

struct ProjectManifest {
  NGIN::UInt32 schemaVersion{4};
  std::string name{};
  std::string type{};
  std::string defaultProfile{};
  std::vector<InputDeclaration> inputs{};
  std::vector<ConditionDefinition> conditions{};
  OutputDefinition output{};
  ProjectBuildDescriptor build{};
  std::vector<GeneratorDeclaration> generators{};
  std::vector<ProjectReference> projectRefs{};
  std::vector<PackageReference> packageRefs{};
  std::vector<PackageFeatureUse> packageFeatureUses{};
  std::vector<EnvironmentDefinition> environments{};
  RuntimeDefinition runtime{};
  std::vector<ProfileDefinition> profiles{};
};

struct ModuleOptions {
  ModuleFamily family{ModuleFamily::App};
  ModuleType type{ModuleType::Runtime};
  StartupStage startupStage{StartupStage::Features};
  SemanticVersion version{0, 1, 0, {}};
  VersionRange compatiblePlatformRange{
      SemanticVersion{0, 1, 0, {}}, SemanticVersion{1, 0, 0, {}}, true, false};
  std::vector<std::string> operatingSystems{};
  std::vector<std::string> architectures{};
  std::vector<DependencyDescriptor> dependencies{};
  std::vector<std::string> providesServices{};
  std::vector<std::string> requiresServices{};
  std::vector<ModuleCapability> capabilities{};
  std::string moduleRoot{};
  bool reflectionRequired{false};
  NGIN::Int32 priority{0};
};

[[nodiscard]] inline auto MakeModuleDescriptor(std::string name,
                                               const ModuleOptions &options = {})
    -> ModuleDescriptor {
  ModuleDescriptor descriptor{};
  descriptor.name = std::move(name);
  descriptor.family = options.family;
  descriptor.type = options.type;
  descriptor.version = options.version;
  descriptor.compatiblePlatformRange = options.compatiblePlatformRange;
  descriptor.operatingSystems = options.operatingSystems;
  descriptor.architectures = options.architectures;
  descriptor.dependencies = options.dependencies;
  descriptor.startupStage = options.startupStage;
  descriptor.entryKind = ModuleEntryKind::Static;
  descriptor.moduleRoot = options.moduleRoot;
  descriptor.providesServices = options.providesServices;
  descriptor.requiresServices = options.requiresServices;
  descriptor.reflectionRequired = options.reflectionRequired;
  descriptor.capabilities = options.capabilities;
  descriptor.priority = options.priority;
  return descriptor;
}

class ServiceCollection {
public:
  virtual ~ServiceCollection() = default;

  template <typename T>
  auto AddSingleton(ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>({}, detail::MakeAutoFactory<T>(),
                         ServiceLifetime::Singleton, std::move(metadata));
  }

  template <typename T>
  auto AddSingleton(std::string name, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    return AddFactory<T>(std::move(name), detail::MakeAutoFactory<T>(),
                         ServiceLifetime::Singleton, std::move(metadata));
  }

  template <typename T>
  auto AddSingleton(NGIN::Memory::Shared<std::remove_cvref_t<T>> service,
                    ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddSingleton<T>({}, std::move(service), std::move(metadata));
  }

  template <typename T>
  auto AddSingleton(std::string name,
                    NGIN::Memory::Shared<std::remove_cvref_t<T>> service,
                    ServiceMetadata metadata = {}) -> ServiceCollection & {
    ServiceRegistrationOptions options{};
    options.lifetime = ServiceLifetime::Singleton;
    options.metadata = std::move(metadata);
    return AddProvider(detail::MakeInstanceProvider<T>(
        std::move(name), std::move(service), std::move(options)));
  }

  template <typename T>
  auto AddSingletonValue(T value, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    using ServiceType = std::remove_cvref_t<T>;
    return AddSingleton<ServiceType>(
        {}, NGIN::Memory::MakeShared<ServiceType>(std::move(value)),
        std::move(metadata));
  }

  template <typename T>
  auto AddSingletonValue(std::string name, T value, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    using ServiceType = std::remove_cvref_t<T>;
    return AddSingleton<ServiceType>(
        std::move(name), NGIN::Memory::MakeShared<ServiceType>(std::move(value)),
        std::move(metadata));
  }

  template <typename T>
  auto AddFactory(ServiceProviderFactory<std::remove_cvref_t<T>> factory,
                  ServiceLifetime lifetime, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    return AddFactory<T>({}, std::move(factory), lifetime, std::move(metadata));
  }

  template <typename T>
  auto AddFactory(std::string name,
                  ServiceProviderFactory<std::remove_cvref_t<T>> factory,
                  ServiceLifetime lifetime, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    ServiceRegistrationOptions options{};
    options.lifetime = lifetime;
    options.metadata = std::move(metadata);
    return AddProvider(detail::MakeFactoryProvider<T>(
        std::move(name), std::move(factory), std::move(options)));
  }

  template <typename T>
  auto AddScoped(ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>({}, detail::MakeAutoFactory<T>(),
                         ServiceLifetime::Scoped, std::move(metadata));
  }

  template <typename T>
  auto AddScoped(std::string name, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    return AddFactory<T>(std::move(name), detail::MakeAutoFactory<T>(),
                         ServiceLifetime::Scoped, std::move(metadata));
  }

  template <typename T>
  auto AddScoped(ServiceProviderFactory<std::remove_cvref_t<T>> factory,
                 ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>({}, std::move(factory), ServiceLifetime::Scoped,
                         std::move(metadata));
  }

  template <typename T>
  auto AddScoped(std::string name,
                 ServiceProviderFactory<std::remove_cvref_t<T>> factory,
                 ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>(std::move(name), std::move(factory),
                         ServiceLifetime::Scoped, std::move(metadata));
  }

  template <typename T>
  auto AddTransient(ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>({}, detail::MakeAutoFactory<T>(),
                         ServiceLifetime::Transient, std::move(metadata));
  }

  template <typename T>
  auto AddTransient(std::string name, ServiceMetadata metadata = {})
      -> ServiceCollection & {
    return AddFactory<T>(std::move(name), detail::MakeAutoFactory<T>(),
                         ServiceLifetime::Transient, std::move(metadata));
  }

  template <typename T>
  auto AddTransient(ServiceProviderFactory<std::remove_cvref_t<T>> factory,
                    ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>({}, std::move(factory), ServiceLifetime::Transient,
                         std::move(metadata));
  }

  template <typename T>
  auto AddTransient(std::string name,
                    ServiceProviderFactory<std::remove_cvref_t<T>> factory,
                    ServiceMetadata metadata = {}) -> ServiceCollection & {
    return AddFactory<T>(std::move(name), std::move(factory),
                         ServiceLifetime::Transient, std::move(metadata));
  }

  virtual auto AddDefaults() -> ServiceCollection & = 0;
  virtual auto AddLogging() -> ServiceCollection & = 0;
  virtual auto AddConfiguration() -> ServiceCollection & = 0;
  virtual auto Clear() -> ServiceCollection & = 0;

protected:
  virtual auto AddProvider(std::shared_ptr<detail::ServiceProviderBase> provider)
      -> ServiceCollection & = 0;
};

class PackageCollection {
public:
  virtual ~PackageCollection() = default;

  virtual auto Add(PackageReference reference) -> PackageCollection & = 0;
  virtual auto AddManifest(PackageManifest manifest) -> PackageCollection & = 0;
  virtual auto AddManifestFile(std::string path) -> PackageCollection & = 0;
  virtual auto RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar)
      -> PackageCollection & = 0;
  virtual auto ApplyBootstrap(std::string packageName)
      -> PackageCollection & = 0;
  virtual auto ApplyBootstrap(std::string packageName, std::string entryPoint)
      -> PackageCollection & = 0;
  virtual auto Clear() -> PackageCollection & = 0;
};

class ModuleCollection {
public:
  virtual ~ModuleCollection() = default;

  virtual auto Register(StaticModuleRegistration registration)
      -> ModuleCollection & = 0;
  virtual auto Enable(std::string moduleName) -> ModuleCollection & = 0;
  virtual auto Disable(std::string moduleName) -> ModuleCollection & = 0;
  virtual auto Clear() -> ModuleCollection & = 0;
};

class PluginCollection {
public:
  virtual ~PluginCollection() = default;

  virtual auto Enable(std::string pluginName) -> PluginCollection & = 0;
  virtual auto Disable(std::string pluginName) -> PluginCollection & = 0;
  virtual auto AddSearchPath(std::string path) -> PluginCollection & = 0;
  virtual auto Clear() -> PluginCollection & = 0;
};

class ConfigurationBuilder {
public:
  virtual ~ConfigurationBuilder() = default;

  virtual auto AddSource(std::string path) -> ConfigurationBuilder & = 0;
  virtual auto SetEnvironmentName(std::string environmentName)
      -> ConfigurationBuilder & = 0;
  virtual auto SetWorkingDirectory(std::string workingDirectory)
      -> ConfigurationBuilder & = 0;
  virtual auto Clear() -> ConfigurationBuilder & = 0;
};

class PackageBootstrapContext {
public:
  virtual ~PackageBootstrapContext() = default;

  [[nodiscard]] virtual auto PackageName() const noexcept
      -> std::string_view = 0;
  [[nodiscard]] virtual auto ProfileName() const noexcept
      -> std::string_view = 0;

  [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection & = 0;
  [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection & = 0;
  [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection & = 0;
  [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection & = 0;
  [[nodiscard]] virtual auto Configuration() noexcept
      -> ConfigurationBuilder & = 0;
};

class PackageBootstrapRegistry {
public:
  virtual ~PackageBootstrapRegistry() = default;

  virtual auto Register(PackageBootstrapEntry entry) -> CoreResult<void> = 0;
  [[nodiscard]] virtual auto Find(std::string_view packageName,
                                  std::string_view entryPoint) const noexcept
      -> const PackageBootstrapEntry * = 0;
  [[nodiscard]] virtual auto
  FindDefault(std::string_view packageName) const noexcept
      -> const PackageBootstrapEntry * = 0;
};

class IApplicationHost {
public:
  virtual ~IApplicationHost() = default;

  virtual auto Start() noexcept -> CoreResult<void> = 0;
  virtual auto Run() noexcept -> CoreResult<void> = 0;
  virtual auto Tick() noexcept -> CoreResult<void> = 0;
  virtual void RequestStop(std::string reason) noexcept = 0;
  virtual auto Shutdown() noexcept -> CoreResult<void> = 0;

  [[nodiscard]] virtual auto GetProfileName() const -> std::string = 0;
  [[nodiscard]] virtual auto GetStartupReport() const -> StartupReport = 0;

  [[nodiscard]] virtual auto GetServices() noexcept
      -> NGIN::Memory::Shared<IServiceRegistry> = 0;
  [[nodiscard]] virtual auto GetConfig() noexcept
      -> NGIN::Memory::Shared<IConfigStore> = 0;
};

class ApplicationBuilder {
public:
  virtual ~ApplicationBuilder() = default;

  virtual auto UseProjectFile(std::string path) -> ApplicationBuilder & = 0;
  virtual auto UseProject(ProjectManifest manifest) -> ApplicationBuilder & = 0;
  virtual auto SetApplicationName(std::string applicationName)
      -> ApplicationBuilder & = 0;
  virtual auto SetProfile(std::string profileName)
      -> ApplicationBuilder & = 0;
  virtual auto AddConfigSource(std::string path) -> ApplicationBuilder & = 0;
  virtual auto AddDefaultServices() -> ApplicationBuilder & = 0;
  virtual auto AddLogging() -> ApplicationBuilder & = 0;
  virtual auto AddConfiguration() -> ApplicationBuilder & = 0;
  virtual auto AddPluginSearchPath(std::string path) -> ApplicationBuilder & = 0;
  virtual auto EnableDynamicPlugins(bool enabled = true)
      -> ApplicationBuilder & = 0;
  virtual auto AddModule(std::string name, ModuleOptions options,
                         ModuleFactory factory) -> ApplicationBuilder & = 0;
  virtual auto
  UseFileSystem(NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem)
      -> ApplicationBuilder & = 0;

  auto AddModule(std::string name, ModuleFactory factory)
      -> ApplicationBuilder & {
    return AddModule(std::move(name), ModuleOptions{}, std::move(factory));
  }

  template <typename TModule>
  auto AddModule(std::string name, ModuleOptions options = {})
      -> ApplicationBuilder & {
    static_assert(std::is_base_of_v<IModule, std::remove_cvref_t<TModule>>,
                  "AddModule<TModule> requires TModule to derive from IModule");
    return AddModule(
        std::move(name), std::move(options),
        []() -> CoreResult<NGIN::Memory::Shared<IModule>> {
          return NGIN::Memory::MakeSharedAs<IModule, std::remove_cvref_t<TModule>>();
        });
  }

  [[nodiscard]] virtual auto Services() noexcept -> ServiceCollection & = 0;
  [[nodiscard]] virtual auto Packages() noexcept -> PackageCollection & = 0;
  [[nodiscard]] virtual auto Modules() noexcept -> ModuleCollection & = 0;
  [[nodiscard]] virtual auto Plugins() noexcept -> PluginCollection & = 0;
  [[nodiscard]] virtual auto Configuration() noexcept
      -> ConfigurationBuilder & = 0;

  [[nodiscard]] virtual auto Build()
      -> CoreResult<std::shared_ptr<IApplicationHost>> = 0;
};

[[nodiscard]] NGIN_CORE_API auto CreateApplicationBuilder(int argc, char **argv)
    -> std::unique_ptr<ApplicationBuilder>;
} // namespace NGIN::Core
