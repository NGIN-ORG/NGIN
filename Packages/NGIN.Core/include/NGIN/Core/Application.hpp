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
};

enum class PackageBootstrapMode : NGIN::UInt8 { BuilderHookV1 };

struct PackageBootstrapDescriptor {
  PackageBootstrapMode mode{PackageBootstrapMode::BuilderHookV1};
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

struct PackageContentFile {
  std::string source{};
  std::string target{};
  std::string kind{};
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
};

struct PackagePluginManifest {
  std::string name{};
  std::vector<std::string> operatingSystems{};
  std::vector<std::string> architectures{};
  std::vector<std::string> requiredModules{};
  std::vector<std::string> optionalModules{};
  bool optional{false};
};

struct PackageManifest {
  NGIN::UInt32 schemaVersion{1};
  std::string name{};
  std::string version{};
  std::string compatiblePlatformRange{};
  std::vector<std::string> operatingSystems{};
  std::vector<std::string> architectures{};
  std::vector<PackageReference> dependencies{};
  std::optional<PackageBootstrapDescriptor> bootstrap{};
  std::vector<PackageContentFile> contents{};
  std::vector<ModuleDescriptor> modules{};
  std::vector<PackagePluginManifest> plugins{};
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

struct RuntimeDefinition {
  std::vector<ModuleDescriptor> modules{};
  std::vector<std::string> enableModules{};
  std::vector<std::string> disableModules{};
};

struct EnvironmentDefinition {
  std::string name{};
  std::vector<ProjectReference> projectRefs{};
  std::vector<PackageReference> packageRefs{};
  std::vector<std::string> configInputs{};
  std::vector<PackageContentFile> contents{};
  std::vector<EnvironmentVariable> variables{};
  std::vector<FeatureFlag> features{};
  RuntimeDefinition runtime{};
};

struct ProfileDefinition {
  std::string name{};
  std::string buildType{"Debug"};
  std::string platform{"linux-x64"};
  std::string operatingSystem{"linux"};
  std::string architecture{"x64"};
  bool enableReflection{false};
  std::string environmentName{};
  std::vector<ProjectReference> projectRefs{};
  std::vector<PackageReference> packageRefs{};
  std::vector<std::string> configInputs{};
  std::optional<LaunchDefinition> launch{};
  RuntimeDefinition runtime{};
};

struct ProjectManifest {
  NGIN::UInt32 schemaVersion{3};
  std::string name{};
  std::string type{};
  std::string defaultProfile{};
  std::vector<std::string> sourceRoots{};
  OutputDefinition output{};
  ProjectBuildDescriptor build{};
  std::vector<ProjectReference> projectRefs{};
  std::vector<PackageReference> packageRefs{};
  std::vector<std::string> configInputs{};
  std::vector<EnvironmentDefinition> environments{};
  RuntimeDefinition runtime{};
  std::vector<ProfileDefinition> profiles{};
};

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
  virtual auto
  UseFileSystem(NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem)
      -> ApplicationBuilder & = 0;

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
