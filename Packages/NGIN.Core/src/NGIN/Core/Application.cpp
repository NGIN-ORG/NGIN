#include <NGIN/Core/Application.hpp>

#include <NGIN/IO/IFileSystem.hpp>
#include <NGIN/IO/Path.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::Core
{
  namespace
  {
    using IoFileSystem = NGIN::IO::IFileSystem;
    using IoPath = NGIN::IO::Path;

    struct PendingServiceRegistration
    {
      std::shared_ptr<detail::ServiceProviderBase> provider{};
    };

    struct PackageBootstrapRequest
    {
      std::string packageName{};
      std::optional<std::string> entryPoint{};
    };

    [[nodiscard]] auto
    MakeBuilderError(const std::string &message, std::string subject = {},
                     const KernelErrorCode code = KernelErrorCode::InvalidArgument)
        -> KernelError
    {
      return MakeKernelError(code, "ApplicationBuilder", std::move(subject),
                             message);
    }

    [[nodiscard]] auto ToString(const IoPath &path) -> std::string
    {
      return std::string(path.View());
    }

    [[nodiscard]] constexpr auto CurrentOperatingSystem() -> std::string_view
    {
#if defined(_WIN32)
      return "windows";
#elif defined(__APPLE__)
      return "macos";
#elif defined(__linux__)
      return "linux";
#else
      return "unknown";
#endif
    }

    [[nodiscard]] constexpr auto CurrentArchitecture() -> std::string_view
    {
#if defined(_M_X64) || defined(__x86_64__)
      return "x64";
#elif defined(_M_IX86) || defined(__i386__)
      return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
      return "arm64";
#elif defined(_M_ARM) || defined(__arm__)
      return "arm";
#else
      return "unknown";
#endif
    }

    template <typename T>
    void AppendUnique(std::vector<T> &target, const T &value)
    {
      if (std::find(target.begin(), target.end(), value) == target.end())
      {
        target.push_back(value);
      }
    }

    void AppendOrReplaceModuleRegistration(
        std::vector<StaticModuleRegistration> &registrations,
        StaticModuleRegistration registration)
    {
      const auto it = std::find_if(registrations.begin(), registrations.end(),
                                   [&](const StaticModuleRegistration &candidate)
                                   {
                                     return candidate.descriptor.name ==
                                            registration.descriptor.name;
                                   });

      if (it == registrations.end())
      {
        registrations.push_back(std::move(registration));
        return;
      }

      *it = std::move(registration);
    }

    [[nodiscard]] auto
    BuildCatalogFrom(const std::vector<StaticModuleRegistration> &registrations,
                     const std::set<std::string> &disabledModules)
        -> CoreResult<NGIN::Memory::Shared<IModuleCatalog>>
    {
      auto catalog = CreateStaticModuleCatalog();
      if (!catalog)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "failed to create module catalog", {}, KernelErrorCode::InternalError));
      }

      for (const auto &registration : registrations)
      {
        if (disabledModules.contains(registration.descriptor.name))
        {
          continue;
        }

        auto result = catalog->Register(registration);
        if (!result)
        {
          return NGIN::Utilities::Unexpected<KernelError>(result.Error());
        }
      }

      return catalog;
    }

    [[nodiscard]] auto ResolveWorkingDirectory(std::string workingDirectory,
                                               const IoPath &projectDirectory)
        -> std::string
    {
      if (workingDirectory.empty())
      {
        return workingDirectory;
      }

      IoPath workPath(workingDirectory);
      if (workPath.IsRelative() && !projectDirectory.IsEmpty())
      {
        return ToString(projectDirectory.Join(workPath.View()).LexicallyNormal());
      }
      return ToString(workPath.LexicallyNormal());
    }

    class PackageBootstrapRegistryImpl final : public PackageBootstrapRegistry
    {
    public:
      explicit PackageBootstrapRegistryImpl(
          std::unordered_map<std::string, std::string> defaultEntryPoints)
          : m_defaultEntryPoints(std::move(defaultEntryPoints)) {}

      auto Register(PackageBootstrapEntry entry) -> CoreResult<void> override
      {
        if (entry.packageName.empty() || entry.entryPoint.empty() ||
            entry.fn == nullptr)
        {
          m_lastError =
              MakeBuilderError("package bootstrap entry must have package name, "
                               "entry point, and function",
                               entry.packageName, KernelErrorCode::InvalidArgument);
          return NGIN::Utilities::Unexpected<KernelError>(*m_lastError);
        }

        const std::string key = entry.packageName + "::" + entry.entryPoint;
        if (m_indexByKey.contains(key))
        {
          m_lastError = MakeBuilderError("duplicate package bootstrap entry", key,
                                         KernelErrorCode::AlreadyExists);
          return NGIN::Utilities::Unexpected<KernelError>(*m_lastError);
        }

        m_entries.push_back(std::move(entry));
        m_indexByKey.emplace(key, m_entries.size() - 1);
        m_lastError.reset();
        return {};
      }

      [[nodiscard]] auto Find(const std::string_view packageName,
                              const std::string_view entryPoint) const noexcept
          -> const PackageBootstrapEntry * override
      {
        const auto it = m_indexByKey.find(std::string(packageName) +
                                          "::" + std::string(entryPoint));
        if (it == m_indexByKey.end())
        {
          return nullptr;
        }
        return &m_entries[it->second];
      }

      [[nodiscard]] auto
      FindDefault(const std::string_view packageName) const noexcept
          -> const PackageBootstrapEntry * override
      {
        const auto defaultIt = m_defaultEntryPoints.find(std::string(packageName));
        if (defaultIt != m_defaultEntryPoints.end())
        {
          return Find(packageName, defaultIt->second);
        }

        const PackageBootstrapEntry *match = nullptr;
        for (const auto &entry : m_entries)
        {
          if (entry.packageName != packageName)
          {
            continue;
          }

          if (match != nullptr)
          {
            return nullptr;
          }
          match = &entry;
        }
        return match;
      }

      [[nodiscard]] auto LastError() const noexcept
          -> const std::optional<KernelError> &
      {
        return m_lastError;
      }

    private:
      std::vector<PackageBootstrapEntry> m_entries{};
      std::unordered_map<std::string, std::size_t> m_indexByKey{};
      std::unordered_map<std::string, std::string> m_defaultEntryPoints{};
      std::optional<KernelError> m_lastError{};
    };

    class ApplicationHostImpl final : public IApplicationHost
    {
    public:
      ApplicationHostImpl(NGIN::Memory::Shared<IKernel> kernel,
                          StartupReport metadataReport,
                          std::shared_ptr<IHostRunLoop> runLoop)
          : m_kernel(std::move(kernel)),
            m_metadataReport(std::move(metadataReport)),
            m_runLoop(std::move(runLoop))
      {
      }

      auto Start() noexcept -> CoreResult<void> override
      {
        return m_kernel->Start();
      }

      auto Run() noexcept -> CoreResult<void> override
      {
        return m_runLoop ? m_runLoop->Run(*this) : m_kernel->Run();
      }

      auto Tick() noexcept -> CoreResult<void> override { return m_kernel->Tick(); }

      void RequestStop(std::string reason) noexcept override
      {
        m_kernel->RequestStop(std::move(reason));
        if (m_runLoop)
        {
          m_runLoop->Wake();
        }
      }

      [[nodiscard]] auto IsStopRequested() const noexcept -> bool override
      {
        return m_kernel->IsStopRequested();
      }

      auto Shutdown() noexcept -> CoreResult<void> override
      {
        return m_kernel->Shutdown();
      }

      [[nodiscard]] auto GetProfileName() const -> std::string override
      {
        return m_metadataReport.targetName;
      }

      [[nodiscard]] auto GetStartupReport() const -> StartupReport override
      {
        auto report = m_kernel->GetStartupReport();
        if (report.hostName.empty())
        {
          report.hostName = m_metadataReport.hostName;
        }
        if (report.targetName.empty())
        {
          report.targetName = m_metadataReport.targetName;
        }
        if (report.hostType.empty())
        {
          report.hostType = m_metadataReport.hostType;
        }

        report.resolvedPackages = m_metadataReport.resolvedPackages;
        report.resolvedPlugins = m_metadataReport.resolvedPlugins;

        report.warnings.insert(report.warnings.end(),
                               m_metadataReport.warnings.begin(),
                               m_metadataReport.warnings.end());

        report.failures.insert(report.failures.end(),
                               m_metadataReport.failures.begin(),
                               m_metadataReport.failures.end());

        return report;
      }

      [[nodiscard]] auto GetServices() noexcept
          -> NGIN::Memory::Shared<IServiceRegistry> override
      {
        return m_kernel->GetServices();
      }

      [[nodiscard]] auto GetConfig() noexcept
          -> NGIN::Memory::Shared<IConfigStore> override
      {
        return m_kernel->GetConfig();
      }

    private:
      NGIN::Memory::Shared<IKernel> m_kernel{};
      StartupReport m_metadataReport{};
      std::shared_ptr<IHostRunLoop> m_runLoop{};
    };

    class ApplicationBuilderImpl;

    class ServiceCollectionImpl final : public ServiceCollection
    {
    public:
      explicit ServiceCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto AddDefaults() -> ServiceCollection & override;
      auto AddLogging() -> ServiceCollection & override;
      auto AddConfiguration() -> ServiceCollection & override;
      auto Clear() -> ServiceCollection & override;

    private:
      auto AddProvider(std::shared_ptr<detail::ServiceProviderBase> provider)
          -> ServiceCollection & override;

      ApplicationBuilderImpl &m_owner;
    };

    class PackageCollectionImpl final : public PackageCollection
    {
    public:
      explicit PackageCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar)
          -> PackageCollection & override;
      auto ApplyBootstrap(std::string packageName) -> PackageCollection & override;
      auto ApplyBootstrap(std::string packageName, std::string entryPoint)
          -> PackageCollection & override;
      auto Clear() -> PackageCollection & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class ModuleCollectionImpl final : public ModuleCollection
    {
    public:
      explicit ModuleCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto Register(StaticModuleRegistration registration)
          -> ModuleCollection & override;
      auto Enable(std::string moduleName) -> ModuleCollection & override;
      auto Disable(std::string moduleName) -> ModuleCollection & override;
      auto Clear() -> ModuleCollection & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class PluginCollectionImpl final : public PluginCollection
    {
    public:
      explicit PluginCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto Enable(std::string pluginName) -> PluginCollection & override;
      auto Disable(std::string pluginName) -> PluginCollection & override;
      auto AddSearchPath(std::string path) -> PluginCollection & override;
      auto Clear() -> PluginCollection & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class ConfigurationBuilderImpl final : public ConfigurationBuilder
    {
    public:
      explicit ConfigurationBuilderImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto AddSource(std::string path) -> ConfigurationBuilder & override;
      auto SetEnvironmentName(std::string environmentName)
          -> ConfigurationBuilder & override;
      auto SetWorkingDirectory(std::string workingDirectory)
          -> ConfigurationBuilder & override;
      auto Clear() -> ConfigurationBuilder & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class PackageBootstrapContextImpl final : public PackageBootstrapContext
    {
    public:
      PackageBootstrapContextImpl(
          std::string_view packageName, std::string_view profileName,
          ServiceCollection &services, PackageCollection &packages,
          ModuleCollection &modules, PluginCollection &plugins,
          ConfigurationBuilder &configuration)
          : m_packageName(packageName), m_profileName(profileName),
            m_services(services), m_packages(packages), m_modules(modules),
            m_plugins(plugins), m_configuration(configuration)
      {
      }

      [[nodiscard]] auto PackageName() const noexcept -> std::string_view override
      {
        return m_packageName;
      }
      [[nodiscard]] auto ProfileName() const noexcept
          -> std::string_view override
      {
        return m_profileName;
      }

      [[nodiscard]] auto Services() noexcept -> ServiceCollection & override
      {
        return m_services;
      }
      [[nodiscard]] auto Packages() noexcept -> PackageCollection & override
      {
        return m_packages;
      }
      [[nodiscard]] auto Modules() noexcept -> ModuleCollection & override
      {
        return m_modules;
      }
      [[nodiscard]] auto Plugins() noexcept -> PluginCollection & override
      {
        return m_plugins;
      }
      [[nodiscard]] auto Configuration() noexcept
          -> ConfigurationBuilder & override
      {
        return m_configuration;
      }

    private:
      std::string_view m_packageName;
      std::string_view m_profileName;
      ServiceCollection &m_services;
      PackageCollection &m_packages;
      ModuleCollection &m_modules;
      PluginCollection &m_plugins;
      ConfigurationBuilder &m_configuration;
    };

    class ApplicationBuilderImpl final : public ApplicationBuilder
    {
    public:
      ApplicationBuilderImpl(const int argc, char **argv)
          : m_services(*this), m_packages(*this), m_modules(*this),
            m_plugins(*this), m_configuration(*this)
      {
        for (int index = 1; index < argc; ++index)
        {
          if (argv[index] != nullptr)
          {
            m_commandLineArgs.emplace_back(argv[index]);
          }
        }
      }

      auto UseFileSystem(NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_fileSystem = std::move(fileSystem);
        }
        return *this;
      }

      auto SetApplicationName(std::string applicationName)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_applicationName = std::move(applicationName);
        }
        return *this;
      }

      auto SetProfile(std::string profileName)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_profileOverride = std::move(profileName);
        }
        return *this;
      }

      auto AddConfigSource(std::string path) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          AppendUnique(m_configInputs, path);
        }
        return *this;
      }

      auto AddDefaultServices() -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_addDefaultServices = true;
        }
        return *this;
      }

      auto AddLogging() -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_addLogging = true;
        }
        return *this;
      }

      auto AddConfiguration() -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_addConfiguration = true;
        }
        return *this;
      }

      auto AddPluginSearchPath(std::string path) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          AppendUnique(m_pluginSearchPaths, path);
          m_enableDynamicPlugins = true;
        }
        return *this;
      }

      auto EnableDynamicPlugins(const bool enabled = true)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_enableDynamicPlugins = enabled;
        }
        return *this;
      }

      auto UseRunLoop(std::shared_ptr<IHostRunLoop> runLoop)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_runLoop = std::move(runLoop);
        }
        return *this;
      }

      auto AddModule(std::string name, ModuleOptions options,
                     ModuleFactory factory) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (HasStickyError())
        {
          return *this;
        }
        if (name.empty() || !factory)
        {
          m_stickyError = MakeBuilderError(
              "module registration must include a name and factory",
              name, KernelErrorCode::InvalidArgument);
          return *this;
        }

        AppendOrReplaceModuleRegistration(
            m_moduleRegistrations,
            StaticModuleRegistration{
                .descriptor = MakeModuleDescriptor(std::move(name), options),
                .factory = std::move(factory),
            });
        return *this;
      }

      [[nodiscard]] auto Services() noexcept -> ServiceCollection & override
      {
        return m_services;
      }
      [[nodiscard]] auto Packages() noexcept -> PackageCollection & override
      {
        return m_packages;
      }
      [[nodiscard]] auto Modules() noexcept -> ModuleCollection & override
      {
        return m_modules;
      }
      [[nodiscard]] auto Plugins() noexcept -> PluginCollection & override
      {
        return m_plugins;
      }
      [[nodiscard]] auto Configuration() noexcept
          -> ConfigurationBuilder & override
      {
        return m_configuration;
      }

      [[nodiscard]] auto Build()
          -> CoreResult<std::shared_ptr<IApplicationHost>> override
      {
        if (m_built)
        {
          return NGIN::Utilities::Unexpected<KernelError>(
              MakeBuilderError("Build() may only be called once", {},
                               KernelErrorCode::InvalidState));
        }
        if (m_stickyError.has_value())
        {
          return NGIN::Utilities::Unexpected<KernelError>(*m_stickyError);
        }

        const HostType hostType = HostType::ConsoleApp;
        const std::string applicationName =
            m_applicationName.empty() ? "NGIN.Application" : m_applicationName;
        const std::string profileName =
            m_profileOverride.empty() ? applicationName : m_profileOverride;

        PackageBootstrapRegistryImpl bootstrapRegistry({});
        for (const auto registrar : m_packageRegistrars)
        {
          registrar(bootstrapRegistry);
          if (bootstrapRegistry.LastError().has_value())
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                *bootstrapRegistry.LastError());
          }
        }

        std::set<std::string> appliedBootstraps{};
        for (std::size_t index = 0;
             index < m_explicitBootstrapRequests.size(); ++index)
        {
          const auto &request = m_explicitBootstrapRequests[index];
          const PackageBootstrapEntry *entry =
              request.entryPoint.has_value()
                  ? bootstrapRegistry.Find(request.packageName,
                                           *request.entryPoint)
                  : bootstrapRegistry.FindDefault(request.packageName);
          if (entry == nullptr || entry->fn == nullptr)
          {
            const auto subject =
                request.entryPoint.has_value()
                    ? request.packageName + "::" + *request.entryPoint
                    : request.packageName;
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "requested package bootstrap entry was not registered",
                subject, KernelErrorCode::NotFound));
          }

          const std::string key = entry->packageName + "::" + entry->entryPoint;
          if (!appliedBootstraps.insert(key).second)
          {
            continue;
          }

          PackageBootstrapContextImpl context(
              entry->packageName, profileName, m_services, m_packages,
              m_modules, m_plugins, m_configuration);
          auto bootstrapResult = entry->fn(context);
          if (!bootstrapResult)
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                bootstrapResult.Error());
          }
        }

        std::vector<std::string> enabledModules = m_enabledModules;
        const std::set<std::string> disabledModuleSet(
            m_disabledModules.begin(), m_disabledModules.end());
        enabledModules.erase(
            std::remove_if(enabledModules.begin(), enabledModules.end(),
                           [&](const std::string &name)
                           {
                             return disabledModuleSet.contains(name);
                           }),
            enabledModules.end());

        std::vector<std::string> enabledPlugins = m_enabledPlugins;
        const std::set<std::string> disabledPluginSet(
            m_disabledPlugins.begin(), m_disabledPlugins.end());
        enabledPlugins.erase(
            std::remove_if(enabledPlugins.begin(), enabledPlugins.end(),
                           [&](const std::string &name)
                           {
                             return disabledPluginSet.contains(name);
                           }),
            enabledPlugins.end());

        auto moduleCatalog =
            BuildCatalogFrom(m_moduleRegistrations, disabledModuleSet);
        if (!moduleCatalog)
        {
          return NGIN::Utilities::Unexpected<KernelError>(moduleCatalog.Error());
        }

        StartupReport metadataReport{};
        metadataReport.hostName = applicationName;
        metadataReport.targetName = profileName;
        metadataReport.hostType = std::string(ToString(hostType));
        for (const auto &request : m_explicitBootstrapRequests)
        {
          AppendUnique(metadataReport.resolvedPackages, request.packageName);
        }
        metadataReport.resolvedPlugins = enabledPlugins;

        KernelHostConfig hostConfig{};
        hostConfig.hostName = applicationName;
        hostConfig.hostType = hostType;
        hostConfig.operatingSystemName = std::string(CurrentOperatingSystem());
        hostConfig.architectureName = std::string(CurrentArchitecture());
        hostConfig.platformName =
            hostConfig.operatingSystemName + "-" + hostConfig.architectureName;
        hostConfig.platformVersion = SemanticVersion{0, 1, 0, {}};
        hostConfig.targetName = profileName;
        hostConfig.workingDirectory =
            ResolveWorkingDirectory(m_workingDirectory, {});
        hostConfig.configInputs = m_configInputs;
        hostConfig.pluginSearchPaths = m_pluginSearchPaths;
        hostConfig.enableDynamicPlugins =
            m_enableDynamicPlugins || !m_pluginSearchPaths.empty();
        hostConfig.enableReflection = false;
        hostConfig.commandLineArgs = m_commandLineArgs;
        hostConfig.environmentName = m_environmentName;
        hostConfig.requestedModules = enabledModules;
        hostConfig.fileSystem = m_fileSystem;
        hostConfig.moduleCatalog = moduleCatalog.Value();

        const auto pendingServices = m_pendingServices;
        const bool addDefaults = m_addDefaultServices;
        const bool addLogging = m_addLogging;
        const bool addConfiguration = m_addConfiguration;

        hostConfig.configureServices =
            [pendingServices, addDefaults, addLogging, addConfiguration,
             profileName](
                KernelBootstrapContext &context) -> CoreResult<void>
        {
          ServiceScopeId hostScope = ServiceScopeId::Global();
          const bool requiresHostScope = std::any_of(
              pendingServices.begin(), pendingServices.end(),
              [](const PendingServiceRegistration &registration)
              {
                return registration.provider &&
                       registration.provider->Options().lifetime !=
                           ServiceLifetime::Singleton;
              });

          if (requiresHostScope)
          {
            auto scope = context.services->BeginScope(ServiceScopeKind::Host,
                                                      profileName);
            if (!scope)
            {
              return NGIN::Utilities::Unexpected<KernelError>(scope.Error());
            }
            hostScope = scope.Value();
          }

          if (addDefaults)
          {
            auto serviceProvider =
                NGIN::Memory::MakeSharedAs<IServiceProvider, detail::ServiceProviderReference>(
                    context.services.Get(), ServiceScopeId::Global());
            auto result = context.services->RegisterSingleton<IServiceProvider>(
                "Core.Services", serviceProvider);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }

            result = context.services->RegisterSingleton<IServiceRegistry>(
                "Core.ServiceRegistry", context.services);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }

            result = context.services->RegisterSingleton<IEventBus>(
                "Core.Events", context.events);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }

            result = context.services->RegisterSingleton<ITaskRuntime>(
                "Core.Tasks", context.tasks);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          if (addConfiguration)
          {
            auto result = context.services->RegisterSingleton<IConfigStore>(
                "Core.Configuration", context.config);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          if (addLogging && context.loggerRegistry != nullptr)
          {
            auto result = context.services->RegisterSingletonValue<NGIN::Log::LoggerRegistry *>(
                "Core.Logging", context.loggerRegistry);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          for (const auto &registration : pendingServices)
          {
            if (!registration.provider)
            {
              continue;
            }

            ServiceRegistrationOptions options{};
            options.lifetime = registration.provider->Options().lifetime;
            options.ownerScope = options.lifetime == ServiceLifetime::Singleton
                                     ? ServiceScopeId::Global()
                                     : hostScope;
            options.metadata = registration.provider->Options().metadata;

            auto result = context.services->RegisterProvider(
                registration.provider->CloneWithOptions(std::move(options)));

            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          return {};
        };

        auto kernel = CreateKernel(hostConfig);
        if (!kernel)
        {
          return NGIN::Utilities::Unexpected<KernelError>(kernel.Error());
        }

        m_built = true;
        std::shared_ptr<IApplicationHost> host =
            std::make_shared<ApplicationHostImpl>(kernel.Value(),
                                                  std::move(metadataReport),
                                                  std::move(m_runLoop));
        return host;
      }

      void MarkMutating()
      {
        if (m_built && !m_stickyError.has_value())
        {
          m_stickyError =
              MakeBuilderError("builder can no longer be modified after Build()",
                               {}, KernelErrorCode::InvalidState);
        }
      }

      [[nodiscard]] auto HasStickyError() const noexcept -> bool
      {
        return m_stickyError.has_value();
      }

      std::vector<PendingServiceRegistration> m_pendingServices{};
      std::vector<PackageBootstrapRegistrarFn> m_packageRegistrars{};
      std::vector<PackageBootstrapRequest> m_explicitBootstrapRequests{};
      std::vector<StaticModuleRegistration> m_moduleRegistrations{};
      std::vector<std::string> m_enabledModules{};
      std::vector<std::string> m_disabledModules{};
      std::vector<std::string> m_enabledPlugins{};
      std::vector<std::string> m_disabledPlugins{};
      std::vector<std::string> m_pluginSearchPaths{};
      bool m_enableDynamicPlugins{false};
      std::vector<std::string> m_configInputs{};
      std::vector<std::string> m_commandLineArgs{};
      NGIN::Memory::Shared<IoFileSystem> m_fileSystem{};
      std::optional<KernelError> m_stickyError{};
      std::string m_applicationName{};
      std::string m_profileOverride{};
      std::string m_environmentName{};
      std::string m_workingDirectory{};
      std::shared_ptr<IHostRunLoop> m_runLoop{};
      bool m_addDefaultServices{false};
      bool m_addLogging{false};
      bool m_addConfiguration{false};
      bool m_built{false};
      ServiceCollectionImpl m_services;
      PackageCollectionImpl m_packages;
      ModuleCollectionImpl m_modules;
      PluginCollectionImpl m_plugins;
      ConfigurationBuilderImpl m_configuration;
    };

    auto ServiceCollectionImpl::AddProvider(
        std::shared_ptr<detail::ServiceProviderBase> provider)
        -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_pendingServices.push_back(PendingServiceRegistration{
            .provider = std::move(provider),
        });
      }
      return *this;
    }

    auto ServiceCollectionImpl::AddDefaults() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_addDefaultServices = true;
      }
      return *this;
    }

    auto ServiceCollectionImpl::AddLogging() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_addLogging = true;
      }
      return *this;
    }

    auto ServiceCollectionImpl::AddConfiguration() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_addConfiguration = true;
      }
      return *this;
    }

    auto ServiceCollectionImpl::Clear() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_pendingServices.clear();
        m_owner.m_addDefaultServices = false;
        m_owner.m_addLogging = false;
        m_owner.m_addConfiguration = false;
      }
      return *this;
    }

    auto PackageCollectionImpl::RegisterLinkedRegistrar(
        PackageBootstrapRegistrarFn registrar) -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (m_owner.HasStickyError())
      {
        return *this;
      }

      if (registrar == nullptr)
      {
        m_owner.m_stickyError =
            MakeBuilderError("package bootstrap registrar must not be null", {},
                             KernelErrorCode::InvalidArgument);
        return *this;
      }

      m_owner.m_packageRegistrars.push_back(registrar);
      return *this;
    }

    auto PackageCollectionImpl::ApplyBootstrap(std::string packageName)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        const auto duplicate =
            std::find_if(m_owner.m_explicitBootstrapRequests.begin(),
                         m_owner.m_explicitBootstrapRequests.end(),
                         [&](const PackageBootstrapRequest &request)
                         {
                           return request.packageName == packageName &&
                                  !request.entryPoint.has_value();
                         });

        if (duplicate == m_owner.m_explicitBootstrapRequests.end())
        {
          m_owner.m_explicitBootstrapRequests.push_back(PackageBootstrapRequest{
              .packageName = std::move(packageName),
              .entryPoint = std::nullopt,
          });
        }
      }
      return *this;
    }

    auto PackageCollectionImpl::ApplyBootstrap(std::string packageName,
                                               std::string entryPoint)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        const auto duplicate =
            std::find_if(m_owner.m_explicitBootstrapRequests.begin(),
                         m_owner.m_explicitBootstrapRequests.end(),
                         [&](const PackageBootstrapRequest &request)
                         {
                           return request.packageName == packageName &&
                                  request.entryPoint.has_value() &&
                                  *request.entryPoint == entryPoint;
                         });

        if (duplicate == m_owner.m_explicitBootstrapRequests.end())
        {
          m_owner.m_explicitBootstrapRequests.push_back(PackageBootstrapRequest{
              .packageName = std::move(packageName),
              .entryPoint = std::move(entryPoint),
          });
        }
      }
      return *this;
    }

    auto PackageCollectionImpl::Clear() -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_packageRegistrars.clear();
        m_owner.m_explicitBootstrapRequests.clear();
      }
      return *this;
    }

    auto ModuleCollectionImpl::Register(StaticModuleRegistration registration)
        -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (m_owner.HasStickyError())
      {
        return *this;
      }

      if (registration.descriptor.name.empty() || !registration.factory)
      {
        m_owner.m_stickyError = MakeBuilderError(
            "module registration must include a descriptor name and factory",
            registration.descriptor.name, KernelErrorCode::InvalidArgument);
        return *this;
      }

      AppendOrReplaceModuleRegistration(m_owner.m_moduleRegistrations,
                                        std::move(registration));
      return *this;
    }

    auto ModuleCollectionImpl::Enable(std::string moduleName)
        -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_enabledModules, moduleName);
      }
      return *this;
    }

    auto ModuleCollectionImpl::Disable(std::string moduleName)
        -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_disabledModules, moduleName);
      }
      return *this;
    }

    auto ModuleCollectionImpl::Clear() -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_moduleRegistrations.clear();
        m_owner.m_enabledModules.clear();
        m_owner.m_disabledModules.clear();
      }
      return *this;
    }

    auto PluginCollectionImpl::Enable(std::string pluginName)
        -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_enabledPlugins, pluginName);
      }
      return *this;
    }

    auto PluginCollectionImpl::Disable(std::string pluginName)
        -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_disabledPlugins, pluginName);
      }
      return *this;
    }

    auto PluginCollectionImpl::AddSearchPath(std::string path)
        -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_pluginSearchPaths, path);
        m_owner.m_enableDynamicPlugins = true;
      }
      return *this;
    }

    auto PluginCollectionImpl::Clear() -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_enabledPlugins.clear();
        m_owner.m_disabledPlugins.clear();
        m_owner.m_pluginSearchPaths.clear();
        m_owner.m_enableDynamicPlugins = false;
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::AddSource(std::string path)
        -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_configInputs, path);
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::SetEnvironmentName(std::string environmentName)
        -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_environmentName = std::move(environmentName);
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::SetWorkingDirectory(std::string workingDirectory)
        -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_workingDirectory = std::move(workingDirectory);
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::Clear() -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_configInputs.clear();
        m_owner.m_environmentName.clear();
        m_owner.m_workingDirectory.clear();
      }
      return *this;
    }
  } // namespace

  auto CreateApplicationBuilder(const int argc, char **argv)
      -> std::unique_ptr<ApplicationBuilder>
  {
    return std::make_unique<ApplicationBuilderImpl>(argc, argv);
  }
} // namespace NGIN::Core
