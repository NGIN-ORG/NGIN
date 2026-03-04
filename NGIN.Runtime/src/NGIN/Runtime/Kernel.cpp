#include <NGIN/Runtime/Kernel.hpp>

#include <NGIN/Log/Log.hpp>
#include <NGIN/Runtime/Platform.hpp>

#include <algorithm>
#include <atomic>
#include <deque>
#include <set>
#include <thread>
#include <unordered_map>

namespace NGIN::Runtime
{
    namespace
    {
        enum class ModuleFamily : NGIN::UInt8
        {
            Base,
            Reflection,
            RuntimeSvc,
            Platform,
            Editor,
            Domain,
            App
        };

        [[nodiscard]] constexpr auto PhaseOrdinal(const LoadPhase phase) noexcept -> NGIN::UInt8
        {
            switch (phase)
            {
                case LoadPhase::Bootstrap: return 0;
                case LoadPhase::Platform: return 1;
                case LoadPhase::CoreServices: return 2;
                case LoadPhase::Data: return 3;
                case LoadPhase::Domain: return 4;
                case LoadPhase::Application: return 5;
                case LoadPhase::Editor: return 6;
            }
            return 255;
        }

        [[nodiscard]] constexpr auto ModuleTypeCompatibleWithHost(const ModuleType type, const HostType host) noexcept -> bool
        {
            switch (host)
            {
                case HostType::RuntimeApp:
                case HostType::Server:
                    return type == ModuleType::Runtime || type == ModuleType::ThirdParty;
                case HostType::EditorHost:
                    return true;
                case HostType::Program:
                    return type == ModuleType::Runtime || type == ModuleType::Program || type == ModuleType::ThirdParty || type == ModuleType::Developer;
            }
            return false;
        }

        [[nodiscard]] auto DetectFamily(const std::string_view moduleName) -> ModuleFamily
        {
            const auto dot = moduleName.find('.');
            const auto prefix = (dot == std::string_view::npos) ? moduleName : moduleName.substr(0, dot);

            if (prefix == "Base" || prefix == "Log")
            {
                return ModuleFamily::Base;
            }
            if (prefix == "Reflection")
            {
                return ModuleFamily::Reflection;
            }
            if (prefix == "Runtime")
            {
                return ModuleFamily::RuntimeSvc;
            }
            if (prefix == "Platform")
            {
                return ModuleFamily::Platform;
            }
            if (prefix == "Editor")
            {
                return ModuleFamily::Editor;
            }
            if (prefix == "Domain")
            {
                return ModuleFamily::Domain;
            }
            return ModuleFamily::App;
        }

        [[nodiscard]] constexpr auto DependencyAllowed(const ModuleFamily src, const ModuleFamily dst) noexcept -> bool
        {
            switch (src)
            {
                case ModuleFamily::Base: return dst == ModuleFamily::Base;
                case ModuleFamily::Reflection: return dst == ModuleFamily::Base;
                case ModuleFamily::RuntimeSvc:
                    return dst == ModuleFamily::Base || dst == ModuleFamily::Reflection || dst == ModuleFamily::RuntimeSvc || dst == ModuleFamily::Platform;
                case ModuleFamily::Platform:
                    return dst == ModuleFamily::Base || dst == ModuleFamily::Reflection || dst == ModuleFamily::Platform;
                case ModuleFamily::Editor:
                    return dst == ModuleFamily::Base || dst == ModuleFamily::Reflection || dst == ModuleFamily::RuntimeSvc || dst == ModuleFamily::Platform || dst == ModuleFamily::Editor || dst == ModuleFamily::Domain;
                case ModuleFamily::Domain:
                    return dst == ModuleFamily::Base || dst == ModuleFamily::Reflection || dst == ModuleFamily::RuntimeSvc || dst == ModuleFamily::Platform || dst == ModuleFamily::Domain;
                case ModuleFamily::App:
                    return dst == ModuleFamily::Base || dst == ModuleFamily::Reflection || dst == ModuleFamily::RuntimeSvc || dst == ModuleFamily::Platform || dst == ModuleFamily::Domain || dst == ModuleFamily::App;
            }
            return false;
        }

        [[nodiscard]] auto ExtractPlatformTag(const std::string& text) -> std::string
        {
            if (text.empty())
            {
                return {};
            }
            const auto dash = text.find('-');
            if (dash == std::string::npos)
            {
                return text;
            }
            return text.substr(0, dash);
        }

        struct ResolvedModule
        {
            StaticModuleRegistration registration {};
            bool                     requiredByGraph {true};
        };
    }

    class KernelImpl final : public IKernel
    {
    public:
        explicit KernelImpl(KernelHostConfig config)
            : m_config(std::move(config))
            , m_state(KernelState::Created)
        {
            if (m_config.hostName.empty())
            {
                m_config.hostName = "NGIN.Host";
            }
            if (m_config.targetName.empty())
            {
                m_config.targetName = "NGIN.Target";
            }
            if (m_config.platformName.empty())
            {
                m_config.platformName = "linux-x64";
            }
        }

        auto Start() noexcept -> RuntimeResult<void> override
        {
            if (m_state == KernelState::Running)
            {
                return RuntimeResult<void> {};
            }
            if (m_state != KernelState::Created && m_state != KernelState::Stopped)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidState, "Kernel", {}, "Start called from invalid state"));
            }

            m_startupReport = {};
            m_stopRequested.store(false, std::memory_order_release);
            m_stopReason.clear();
            m_moduleInfos.clear();
            m_moduleInstances.clear();

            SetState(KernelState::ConfigLoaded);
            ApplyConfigSources();

            auto resolve = ResolveModules();
            if (!resolve)
            {
                SetState(KernelState::Stopped);
                SetState(KernelState::Shutdown);
                return NGIN::Utilities::Unexpected<KernelError>(resolve.ErrorUnsafe());
            }

            SetState(KernelState::ModulesResolved);

            auto build = BuildSubsystems();
            if (!build)
            {
                SetState(KernelState::Stopped);
                SetState(KernelState::Shutdown);
                return NGIN::Utilities::Unexpected<KernelError>(build.ErrorUnsafe());
            }

            SetState(KernelState::ServicesBuilt);

            auto load = LoadAndStartModules();
            if (!load)
            {
                auto shutdown = Shutdown();
                if (!shutdown)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(shutdown.ErrorUnsafe());
                }
                return NGIN::Utilities::Unexpected<KernelError>(load.ErrorUnsafe());
            }

            SetState(KernelState::ModulesLoaded);
            SetState(KernelState::Running);
            LogCategory("Kernel", "kernel entered Running state");
            return RuntimeResult<void> {};
        }

        auto Run() noexcept -> RuntimeResult<void> override
        {
            if (m_state == KernelState::Created)
            {
                auto start = Start();
                if (!start)
                {
                    return start;
                }
            }

            if (m_state != KernelState::Running)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidState, "Kernel", {}, "Run requires Running state"));
            }

            while (!m_stopRequested.load(std::memory_order_acquire))
            {
                auto tick = Tick();
                if (!tick)
                {
                    switch (m_config.failurePolicy)
                    {
                        case KernelFailurePolicy::FailFast:
                            return tick;
                        case KernelFailurePolicy::StopKernel:
                        case KernelFailurePolicy::IsolateModule:
                            RequestStop("tick failure");
                            break;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return Shutdown();
        }

        auto Tick() noexcept -> RuntimeResult<void> override
        {
            if (m_state != KernelState::Running)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidState, "Kernel", {}, "Tick requires Running state"));
            }

            if (m_events)
            {
                auto flush = m_events->FlushDeferred();
                if (!flush)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(flush.ErrorUnsafe());
                }
            }

            return RuntimeResult<void> {};
        }

        void RequestStop(std::string reason) noexcept override
        {
            m_stopReason = std::move(reason);
            m_stopRequested.store(true, std::memory_order_release);
            LogCategory("Kernel", "stop requested: " + m_stopReason);
        }

        auto Shutdown() noexcept -> RuntimeResult<void> override
        {
            if (m_state == KernelState::Shutdown)
            {
                return RuntimeResult<void> {};
            }

            if (m_state == KernelState::Created)
            {
                SetState(KernelState::Stopped);
                SetState(KernelState::Shutdown);
                return RuntimeResult<void> {};
            }

            SetState(KernelState::Stopping);
            StopAndShutdownModules();

            if (m_tasks)
            {
                const auto wait = m_tasks->WaitIdle(std::chrono::milliseconds(2000));
                if (!wait)
                {
                    LogCategory("Tasks", "wait idle failed during shutdown");
                }
            }

            SetState(KernelState::Stopped);
            SetState(KernelState::Shutdown);
            LogCategory("Kernel", "kernel shutdown complete");
            return RuntimeResult<void> {};
        }

        [[nodiscard]] auto GetState() const noexcept -> KernelState override
        {
            return m_state;
        }

        [[nodiscard]] auto GetStartupReport() const -> StartupReport override
        {
            return m_startupReport;
        }

        [[nodiscard]] auto GetModuleStates() const -> std::vector<ModuleRuntimeInfo> override
        {
            return m_moduleInfos;
        }

        [[nodiscard]] auto GetServices() noexcept -> NGIN::Memory::Shared<IServiceRegistry> override
        {
            return m_services;
        }

        [[nodiscard]] auto GetEvents() noexcept -> NGIN::Memory::Shared<IEventBus> override
        {
            return m_events;
        }

        [[nodiscard]] auto GetTasks() noexcept -> NGIN::Memory::Shared<ITaskRuntime> override
        {
            return m_tasks;
        }

        [[nodiscard]] auto GetConfig() noexcept -> NGIN::Memory::Shared<IConfigStore> override
        {
            return m_configStore;
        }

    private:
        void SetState(const KernelState state) noexcept
        {
            m_state = state;
            LogCategory("Kernel", "state transition -> " + std::string(ToString(state)));
        }

        void ApplyConfigSources()
        {
            m_configStore = CreateConfigStore();
            if (!m_configStore)
            {
                return;
            }

            auto set = m_configStore->SetValue(ConfigLayer::BuiltInDefaults, "Kernel.HostName", m_config.hostName);
            (void)set;
            set = m_configStore->SetValue(ConfigLayer::BuiltInDefaults, "Kernel.TargetName", m_config.targetName);
            (void)set;
            set = m_configStore->SetValue(ConfigLayer::BuiltInDefaults, "Kernel.Platform", m_config.platformName);
            (void)set;
            set = m_configStore->SetValue(ConfigLayer::BuiltInDefaults, "Kernel.HostType", std::string(ToString(m_config.hostType)));
            (void)set;
            set = m_configStore->SetValue(ConfigLayer::BuiltInDefaults, "Kernel.ReflectionEnabled", m_config.enableReflection ? "true" : "false");
            (void)set;
        }

        auto BuildSubsystems() noexcept -> RuntimeResult<void>
        {
            if (!m_configStore)
            {
                m_configStore = CreateConfigStore();
            }

            m_services = CreateServiceRegistry();
            m_events = CreateEventBus();
            m_tasks = CreateTaskRuntime(m_config.schedulerPolicy.workerThreads);

            if (!m_services || !m_events || !m_tasks || !m_configStore)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InternalError, "Kernel", {}, "failed to allocate required subsystems"));
            }

            NGIN::Log::LoggerRegistry::SinkSet sinks {};
            if (m_config.logSinkConfig.includeConsoleSink)
            {
                sinks.push_back(
                    NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>(
                        NGIN::Log::ConsoleSinkOptions {
                            .useStderrForErrors = true,
                            .includeSource = m_config.logSinkConfig.includeSource,
                            .autoFlush = m_config.logSinkConfig.autoFlush}));
            }
            m_loggerRegistry.SetDefaultSinks(std::move(sinks));

            return RuntimeResult<void> {};
        }

        [[nodiscard]] auto ModuleByName(const std::string& name) const -> const ModuleDescriptor*
        {
            const auto it = m_moduleDescriptorByName.find(name);
            if (it == m_moduleDescriptorByName.end())
            {
                return nullptr;
            }
            return &it->second;
        }

        auto ResolveModules() noexcept -> RuntimeResult<void>
        {
            m_resolvedModules.clear();
            m_moduleDescriptorByName.clear();

            auto staticModules = GetStaticModules();
            std::unordered_map<std::string, StaticModuleRegistration> catalog {};
            catalog.reserve(staticModules.size());

            for (auto& reg : staticModules)
            {
                if (catalog.contains(reg.descriptor.name))
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::AlreadyExists, "ModuleLoader", reg.descriptor.name, "duplicate static module descriptor"));
                }
                catalog.emplace(reg.descriptor.name, std::move(reg));
            }

            if (m_config.enableDynamicPlugins && m_config.pluginCatalog)
            {
                std::vector<ModuleDescriptor> dynamicDescriptors;
                auto collect = m_config.pluginCatalog->CollectDescriptors(dynamicDescriptors);
                if (!collect)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(collect.ErrorUnsafe());
                }

                for (auto& descriptor : dynamicDescriptors)
                {
                    descriptor.entryKind = ModuleEntryKind::Dynamic;
                    StaticModuleRegistration registration {
                        .descriptor = std::move(descriptor),
                        .factory = {},
                    };
                    if (catalog.contains(registration.descriptor.name))
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::AlreadyExists, "ModuleLoader", registration.descriptor.name, "duplicate dynamic/static module descriptor"));
                    }
                    catalog.emplace(registration.descriptor.name, std::move(registration));
                }
            }

            std::unordered_map<std::string, bool> requiredMap {};
            std::deque<std::pair<std::string, bool>> queue;

            if (m_config.requestedModules.empty())
            {
                for (const auto& [name, _] : catalog)
                {
                    queue.emplace_back(name, true);
                }
            }
            else
            {
                for (const auto& name : m_config.requestedModules)
                {
                    queue.emplace_back(name, true);
                }
            }

            while (!queue.empty())
            {
                auto [name, required] = queue.front();
                queue.pop_front();

                auto catIt = catalog.find(name);
                if (catIt == catalog.end())
                {
                    if (required)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::NotFound, "ModuleLoader", name, "requested module was not found in catalog"));
                    }
                    m_startupReport.warnings.push_back({"ModuleLoader", name, "optional module not found"});
                    continue;
                }

                const auto requiredIt = requiredMap.find(name);
                if (requiredIt != requiredMap.end())
                {
                    if (required && !requiredIt->second)
                    {
                        requiredIt->second = true;
                    }
                    continue;
                }

                requiredMap.emplace(name, required);
                const auto& descriptor = catIt->second.descriptor;

                for (const auto& dep : descriptor.dependencies)
                {
                    queue.emplace_back(dep.name, !dep.optional);
                }
            }

            // Compatibility pass + prune unsupported optional modules.
            std::set<std::string> active;
            for (const auto& [name, _] : requiredMap)
            {
                active.insert(name);
            }

            bool changed = true;
            while (changed)
            {
                changed = false;
                for (auto it = active.begin(); it != active.end();)
                {
                    const auto& name = *it;
                    const auto catIt = catalog.find(name);
                    if (catIt == catalog.end())
                    {
                        it = active.erase(it);
                        changed = true;
                        continue;
                    }

                    const auto& desc = catIt->second.descriptor;
                    const bool required = requiredMap[name];

                    const auto platformTag = ExtractPlatformTag(m_config.platformName);
                    const bool platformOk = desc.platforms.empty() || std::find(desc.platforms.begin(), desc.platforms.end(), platformTag) != desc.platforms.end();
                    const bool hostOk = ModuleTypeCompatibleWithHost(desc.type, m_config.hostType)
                        && !(desc.loadPhase == LoadPhase::Editor && m_config.hostType != HostType::EditorHost)
                        && (!desc.reflectionRequired || m_config.enableReflection);

                    if (!platformOk || !hostOk)
                    {
                        if (required)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(
                                MakeKernelError(KernelErrorCode::IncompatibleHostType, "ModuleLoader", desc.name, "module incompatible with host/platform settings"));
                        }

                        m_startupReport.warnings.push_back({"ModuleLoader", desc.name, "optional module skipped due compatibility"});
                        m_startupReport.skippedOptionalModules.push_back(desc.name);
                        it = active.erase(it);
                        changed = true;
                        continue;
                    }

                    if (desc.entryKind == ModuleEntryKind::Dynamic && !m_config.enableDynamicPlugins)
                    {
                        if (required)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(
                                MakeKernelError(KernelErrorCode::DynamicPluginUnsupported, "Plugin", desc.name, "dynamic module requested while dynamic plugins are disabled"));
                        }
                        m_startupReport.warnings.push_back({"Plugin", desc.name, "optional dynamic module skipped (dynamic disabled)"});
                        it = active.erase(it);
                        changed = true;
                        continue;
                    }

                    bool missingRequiredDependency = false;
                    for (const auto& dep : desc.dependencies)
                    {
                        if (!dep.optional && !active.contains(dep.name))
                        {
                            missingRequiredDependency = true;
                            break;
                        }
                    }

                    if (missingRequiredDependency)
                    {
                        if (required)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(
                                MakeKernelError(
                                    KernelErrorCode::MissingRequiredDependency,
                                    "ModuleLoader",
                                    desc.name,
                                    "required dependency missing after compatibility filtering"));
                        }

                        m_startupReport.warnings.push_back({"ModuleLoader", desc.name, "optional module skipped due missing required dependency"});
                        m_startupReport.skippedOptionalModules.push_back(desc.name);
                        it = active.erase(it);
                        changed = true;
                        continue;
                    }

                    ++it;
                }
            }

            // Dependency and phase validation + layer constraints.
            std::unordered_map<std::string, std::vector<std::string>> dependents {};
            std::unordered_map<std::string, NGIN::UInt32> indegree {};
            std::unordered_map<std::string, StaticModuleRegistration> selected {};

            for (const auto& name : active)
            {
                const auto catIt = catalog.find(name);
                if (catIt == catalog.end())
                {
                    continue;
                }

                selected.emplace(name, catIt->second);
                indegree.emplace(name, 0);
                m_moduleDescriptorByName.emplace(name, catIt->second.descriptor);
            }

            for (const auto& [name, registration] : selected)
            {
                const auto srcFamily = DetectFamily(name);

                for (const auto& dep : registration.descriptor.dependencies)
                {
                    if (!active.contains(dep.name))
                    {
                        if (dep.optional)
                        {
                            m_startupReport.warnings.push_back({"ModuleLoader", name, "optional dependency missing: " + dep.name});
                            continue;
                        }
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::MissingRequiredDependency, "ModuleLoader", name, "required dependency missing: " + dep.name));
                    }

                    const auto* depDesc = ModuleByName(dep.name);
                    if (!depDesc)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::InternalError, "ModuleLoader", name, "dependency descriptor lookup failed: " + dep.name));
                    }

                    const auto dstFamily = DetectFamily(dep.name);
                    if (!DependencyAllowed(srcFamily, dstFamily))
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::LayerConstraintViolation, "ModuleLoader", name, "forbidden dependency edge to " + dep.name));
                    }

                    if (PhaseOrdinal(depDesc->loadPhase) > PhaseOrdinal(registration.descriptor.loadPhase))
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::PhaseOrderingViolation, "ModuleLoader", name, "dependency phase is later than dependent module"));
                    }

                    if (!dep.optional)
                    {
                        indegree[name] += 1;
                        dependents[dep.name].push_back(name);
                    }
                }
            }

            std::vector<std::string> ready;
            ready.reserve(indegree.size());
            for (const auto& [name, count] : indegree)
            {
                if (count == 0)
                {
                    ready.push_back(name);
                }
            }

            auto sortReady = [&]() {
                std::sort(
                    ready.begin(),
                    ready.end(),
                    [&](const std::string& lhs, const std::string& rhs) {
                        const auto* lhsDesc = ModuleByName(lhs);
                        const auto* rhsDesc = ModuleByName(rhs);
                        if (!lhsDesc || !rhsDesc)
                        {
                            return lhs < rhs;
                        }
                        const auto lhsPhase = PhaseOrdinal(lhsDesc->loadPhase);
                        const auto rhsPhase = PhaseOrdinal(rhsDesc->loadPhase);
                        if (lhsPhase != rhsPhase)
                        {
                            return lhsPhase < rhsPhase;
                        }
                        if (lhsDesc->priority != rhsDesc->priority)
                        {
                            return lhsDesc->priority < rhsDesc->priority;
                        }
                        return lhs < rhs;
                    });
            };

            sortReady();

            std::vector<std::string> ordered;
            ordered.reserve(indegree.size());

            while (!ready.empty())
            {
                const std::string current = ready.front();
                ready.erase(ready.begin());
                ordered.push_back(current);

                auto depIt = dependents.find(current);
                if (depIt == dependents.end())
                {
                    continue;
                }

                for (const auto& dependent : depIt->second)
                {
                    auto inIt = indegree.find(dependent);
                    if (inIt == indegree.end())
                    {
                        continue;
                    }

                    if (inIt->second > 0)
                    {
                        inIt->second -= 1;
                        if (inIt->second == 0)
                        {
                            ready.push_back(dependent);
                        }
                    }
                }
                sortReady();
            }

            if (ordered.size() != indegree.size())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::DependencyCycle, "ModuleLoader", {}, "required dependency cycle detected"));
            }

            m_resolvedModules.reserve(ordered.size());
            for (const auto& name : ordered)
            {
                const auto selectedIt = selected.find(name);
                if (selectedIt == selected.end())
                {
                    continue;
                }
                m_resolvedModules.push_back(ResolvedModule {
                    .registration = selectedIt->second,
                    .requiredByGraph = requiredMap[name],
                });
                m_startupReport.resolvedModules.push_back(name);
            }

            return RuntimeResult<void> {};
        }

        auto LoadAndStartModules() noexcept -> RuntimeResult<void>
        {
            m_moduleInfos.reserve(m_resolvedModules.size());
            m_moduleInstances.resize(m_resolvedModules.size());

            for (std::size_t index = 0; index < m_resolvedModules.size(); ++index)
            {
                auto& resolved = m_resolvedModules[index];
                const auto& descriptor = resolved.registration.descriptor;

                ModuleRuntimeInfo info {
                    .descriptor = descriptor,
                    .state = ModuleState::Loaded,
                    .optional = !resolved.requiredByGraph,
                    .lastError = {},
                };
                m_moduleInfos.push_back(info);

                if (descriptor.entryKind == ModuleEntryKind::Dynamic)
                {
                    if (!m_config.enableDynamicPlugins)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::DynamicPluginUnsupported, "Plugin", descriptor.name, "dynamic module requested while dynamic plugins are disabled"));
                    }
                    if (!m_config.pluginBinaryLoader)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(KernelErrorCode::DynamicPluginUnsupported, "Plugin", descriptor.name, "dynamic module requires Spec 003 binary loader"));
                    }

                    auto load = m_config.pluginBinaryLoader->LoadBinary(descriptor);
                    if (!load)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(load.ErrorUnsafe());
                    }
                }

                if (!resolved.registration.factory)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::ModuleFactoryFailure, "ModuleLoader", descriptor.name, "module factory is missing"));
                }

                auto instance = resolved.registration.factory();
                if (!instance)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(instance.ErrorUnsafe());
                }

                m_moduleInstances[index] = instance.ValueUnsafe();
                m_moduleInfos[index].state = ModuleState::Constructed;

                ModuleContext context(
                    descriptor.name,
                    *m_services,
                    *m_events,
                    *m_tasks,
                    *m_configStore,
                    m_loggerRegistry,
                    [&]() { return m_stopRequested.load(std::memory_order_acquire); });

                auto reg = m_moduleInstances[index]->OnRegister(context);
                if (!reg)
                {
                    m_moduleInfos[index].lastError = reg.ErrorUnsafe().message;
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::ModuleLifecycleFailure, "Module", descriptor.name, "OnRegister failed"));
                }

                auto init = m_moduleInstances[index]->OnInit(context);
                if (!init)
                {
                    m_moduleInfos[index].lastError = init.ErrorUnsafe().message;
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::ModuleLifecycleFailure, "Module", descriptor.name, "OnInit failed"));
                }

                m_moduleInfos[index].state = ModuleState::Initialized;

                auto start = m_moduleInstances[index]->OnStart(context);
                if (!start)
                {
                    m_moduleInfos[index].lastError = start.ErrorUnsafe().message;
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::ModuleLifecycleFailure, "Module", descriptor.name, "OnStart failed"));
                }

                m_moduleInfos[index].state = ModuleState::Running;
                LogCategory("ModuleLoader", "module started: " + descriptor.name);
            }

            return RuntimeResult<void> {};
        }

        void StopAndShutdownModules() noexcept
        {
            if (!m_services || !m_events || !m_tasks || !m_configStore)
            {
                return;
            }

            for (std::size_t i = m_moduleInfos.size(); i > 0; --i)
            {
                const std::size_t index = i - 1;
                auto& info = m_moduleInfos[index];
                const auto& descriptor = info.descriptor;

                if (!m_moduleInstances[index])
                {
                    continue;
                }

                ModuleContext context(
                    descriptor.name,
                    *m_services,
                    *m_events,
                    *m_tasks,
                    *m_configStore,
                    m_loggerRegistry,
                    [&]() { return m_stopRequested.load(std::memory_order_acquire); });

                info.state = ModuleState::Stopping;
                auto stop = m_moduleInstances[index]->OnStop(context);
                if (!stop)
                {
                    info.lastError = stop.ErrorUnsafe().message;
                }

                m_events->ClearScope(EventScope {.owner = descriptor.name});
                m_services->ClearScope(ServiceScope {.lifetime = ServiceLifetime::Module, .owner = descriptor.name});

                auto shutdown = m_moduleInstances[index]->OnShutdown(context);
                if (!shutdown)
                {
                    info.lastError = shutdown.ErrorUnsafe().message;
                }

                info.state = ModuleState::Unloaded;
            }
        }

        void LogCategory(const std::string_view category, const std::string& message)
        {
            auto logger = m_loggerRegistry.GetOrCreate(std::string(category), NGIN::Log::LogLevel::Info);
            if (!logger)
            {
                return;
            }

            logger->Info([&](NGIN::Log::RecordBuilder& record) {
                record.Message(message);
                record.Attr("host", std::string_view(m_config.hostName));
            });
        }

    private:
        KernelHostConfig m_config;
        KernelState      m_state {KernelState::Created};

        std::atomic<bool> m_stopRequested {false};
        std::string       m_stopReason {};

        NGIN::Memory::Shared<IServiceRegistry> m_services {};
        NGIN::Memory::Shared<IEventBus>        m_events {};
        NGIN::Memory::Shared<ITaskRuntime>     m_tasks {};
        NGIN::Memory::Shared<IConfigStore>     m_configStore {};

        NGIN::Log::LoggerRegistry m_loggerRegistry {};

        StartupReport m_startupReport {};
        std::unordered_map<std::string, ModuleDescriptor> m_moduleDescriptorByName {};
        std::vector<ResolvedModule> m_resolvedModules {};
        std::vector<ModuleRuntimeInfo> m_moduleInfos {};
        std::vector<NGIN::Memory::Shared<IModule>> m_moduleInstances {};
    };

    auto CreateKernel(const KernelHostConfig& config) noexcept -> RuntimeResult<NGIN::Memory::Shared<IKernel>>
    {
        auto kernel = NGIN::Memory::MakeSharedAs<IKernel, KernelImpl>(config);
        if (!kernel)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InternalError, "Kernel", {}, "failed to allocate kernel instance"));
        }

        return kernel;
    }
}

