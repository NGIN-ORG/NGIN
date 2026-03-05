#include <catch2/catch_test_macros.hpp>

#include <NGIN/Core/Core.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] auto Range(const std::string_view text) -> NGIN::Core::VersionRange
    {
        auto parsed = NGIN::Core::ParseVersionRange(text);
        REQUIRE(parsed.HasValue());
        return parsed.ValueUnsafe();
    }

    [[nodiscard]] auto Version(
        const NGIN::UInt32 major,
        const NGIN::UInt32 minor,
        const NGIN::UInt32 patch) -> NGIN::Core::SemanticVersion
    {
        return NGIN::Core::SemanticVersion {major, minor, patch, {}};
    }

    [[nodiscard]] auto MakeDescriptor(
        std::string name,
        const NGIN::Core::ModuleFamily family = NGIN::Core::ModuleFamily::Core,
        const NGIN::Core::LoadPhase phase = NGIN::Core::LoadPhase::CoreServices,
        const NGIN::Core::ModuleType type = NGIN::Core::ModuleType::Runtime) -> NGIN::Core::ModuleDescriptor
    {
        NGIN::Core::ModuleDescriptor desc {};
        desc.name = std::move(name);
        desc.family = family;
        desc.type = type;
        desc.version = Version(0, 1, 0);
        desc.compatiblePlatformRange = Range(">=0.1.0 <1.0.0");
        desc.platforms = {"linux", "windows", "macos"};
        desc.loadPhase = phase;
        desc.entryKind = NGIN::Core::ModuleEntryKind::Static;
        return desc;
    }

    [[nodiscard]] auto MakeHostConfig(
        const NGIN::Memory::Shared<NGIN::Core::IModuleCatalog>& catalog) -> NGIN::Core::KernelHostConfig
    {
        NGIN::Core::KernelHostConfig cfg {};
        cfg.hostName = "Core.Tests";
        cfg.hostType = NGIN::Core::HostType::ConsoleApp;
        cfg.platformName = "linux-x64";
        cfg.platformVersion = Version(0, 1, 0);
        cfg.targetName = "Core.Tests.Target";
        cfg.enableDynamicPlugins = false;
        cfg.enableReflection = false;
        cfg.logSinkConfig.includeConsoleSink = false;
        cfg.moduleCatalog = catalog;
        cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::Serialized;
        return cfg;
    }

    auto RegisterModule(
        const NGIN::Memory::Shared<NGIN::Core::IModuleCatalog>& catalog,
        NGIN::Core::ModuleDescriptor descriptor,
        NGIN::Core::ModuleFactory factory) -> NGIN::Core::CoreResult<void>
    {
        return catalog->Register(NGIN::Core::StaticModuleRegistration {
            .descriptor = std::move(descriptor),
            .factory = std::move(factory),
        });
    }

    struct GlobalStaticModuleGuard
    {
        GlobalStaticModuleGuard()
        {
            NGIN::Core::ClearStaticModules();
        }

        ~GlobalStaticModuleGuard()
        {
            NGIN::Core::ClearStaticModules();
        }
    };

    void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path);
        REQUIRE(output.good());
        output << text;
        output.close();
    }

    class HookModule final : public NGIN::Core::IModule
    {
    public:
        enum class FailStage
        {
            None,
            Register,
            Init,
            Start
        };

        HookModule(
            std::string name,
            std::vector<std::string>* order,
            std::mutex* lock,
            const FailStage failStage = FailStage::None)
            : m_name(std::move(name))
            , m_order(order)
            , m_lock(lock)
            , m_failStage(failStage)
        {
        }

        auto OnRegister(NGIN::Core::ModuleContext&) noexcept -> NGIN::Core::CoreResult<void> override
        {
            Push("register:" + m_name);
            if (m_failStage == FailStage::Register)
            {
                return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
                    NGIN::Core::MakeKernelError(
                        NGIN::Core::KernelErrorCode::ModuleLifecycleFailure,
                        "Test",
                        m_name,
                        "forced register failure"));
            }
            return {};
        }

        auto OnInit(NGIN::Core::ModuleContext&) noexcept -> NGIN::Core::CoreResult<void> override
        {
            Push("init:" + m_name);
            if (m_failStage == FailStage::Init)
            {
                return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
                    NGIN::Core::MakeKernelError(
                        NGIN::Core::KernelErrorCode::ModuleLifecycleFailure,
                        "Test",
                        m_name,
                        "forced init failure"));
            }
            return {};
        }

        auto OnStart(NGIN::Core::ModuleContext&) noexcept -> NGIN::Core::CoreResult<void> override
        {
            Push("start:" + m_name);
            if (m_failStage == FailStage::Start)
            {
                return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
                    NGIN::Core::MakeKernelError(
                        NGIN::Core::KernelErrorCode::ModuleLifecycleFailure,
                        "Test",
                        m_name,
                        "forced start failure"));
            }
            return {};
        }

        auto OnStop(NGIN::Core::ModuleContext&) noexcept -> NGIN::Core::CoreResult<void> override
        {
            Push("stop:" + m_name);
            return {};
        }

        auto OnShutdown(NGIN::Core::ModuleContext&) noexcept -> NGIN::Core::CoreResult<void> override
        {
            Push("shutdown:" + m_name);
            return {};
        }

    private:
        void Push(const std::string& value)
        {
            if (m_order == nullptr || m_lock == nullptr)
            {
                return;
            }
            std::lock_guard<std::mutex> guard(*m_lock);
            m_order->push_back(value);
        }

        std::string               m_name;
        std::vector<std::string>* m_order {nullptr};
        std::mutex*               m_lock {nullptr};
        FailStage                 m_failStage {FailStage::None};
    };

    class ScopedServiceModule final : public NGIN::Core::IModule
    {
    public:
        explicit ScopedServiceModule(std::string key)
            : m_key(std::move(key))
        {
        }

        auto OnRegister(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            return context.RegisterFactory(
                m_key,
                []() -> NGIN::Core::CoreResult<NGIN::Utilities::Any<>>
                {
                    return NGIN::Utilities::Any<>(NGIN::UInt32 {42});
                },
                NGIN::Core::ServiceLifetime::Scoped);
        }

    private:
        std::string m_key;
    };

    class ServiceProviderModule final : public NGIN::Core::IModule
    {
    public:
        explicit ServiceProviderModule(std::string key)
            : m_key(std::move(key))
        {
        }

        auto OnRegister(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            return context.RegisterSingleton(m_key, NGIN::Utilities::Any<>(std::string("provided")));
        }

    private:
        std::string m_key;
    };

    class EventProbeModule final : public NGIN::Core::IModule
    {
    public:
        EventProbeModule(std::vector<std::string>* events, std::mutex* lock)
            : m_events(events), m_lock(lock)
        {
        }

        auto OnRegister(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            for (const auto channel : {"ModuleLoaded", "ModuleStarted", "KernelRunning", "KernelStopping"})
            {
                auto sub = context.Events().Subscribe(
                    channel,
                    [this](const NGIN::Core::EventRecord& eventRecord) {
                        if (m_events == nullptr || m_lock == nullptr)
                        {
                            return;
                        }
                        std::lock_guard<std::mutex> guard(*m_lock);
                        m_events->push_back(eventRecord.channel);
                    },
                    NGIN::Core::EventScope {.owner = std::string(context.ModuleName())});
                if (!sub)
                {
                    return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(sub.ErrorUnsafe());
                }
                m_tokens.push_back(sub.ValueUnsafe());
            }

            return {};
        }

    private:
        std::vector<std::string>*                       m_events {nullptr};
        std::mutex*                                     m_lock {nullptr};
        std::vector<NGIN::Core::EventSubscriptionToken> m_tokens {};
    };
}

TEST_CASE("KernelStartsWithDependencyOrderedModules", "[runtime][startup]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    std::vector<std::string> order;
    std::mutex orderLock;

    auto a = MakeDescriptor("Core.A", NGIN::Core::ModuleFamily::Core, NGIN::Core::LoadPhase::Bootstrap);
    auto b = MakeDescriptor("Core.B");
    b.dependencies.push_back(NGIN::Core::DependencyDescriptor {
        .name = "Core.A",
        .optional = false,
        .requiredVersion = Range(">=0.1.0 <1.0.0"),
    });

    REQUIRE(RegisterModule(
                catalog,
                a,
                [&]() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("A", &order, &orderLock);
                })
                .HasValue());
    REQUIRE(RegisterModule(
                catalog,
                b,
                [&]() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("B", &order, &orderLock);
                })
                .HasValue());

    auto kernelResult = NGIN::Core::CreateKernel(MakeHostConfig(catalog));
    REQUIRE(kernelResult.HasValue());
    auto kernel = kernelResult.ValueUnsafe();

    REQUIRE(kernel->Start().HasValue());
    REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Running);

    const auto report = kernel->GetStartupReport();
    REQUIRE(report.resolvedModules.size() == 2);
    REQUIRE(report.failures.empty());

    REQUIRE(kernel->Shutdown().HasValue());
    REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Shutdown);

    const auto startA = std::find(order.begin(), order.end(), "start:A");
    const auto startB = std::find(order.begin(), order.end(), "start:B");
    REQUIRE(startA != order.end());
    REQUIRE(startB != order.end());
    REQUIRE(startA < startB);
}

TEST_CASE("MissingRequiredDependencyFailsBeforeLifecycle", "[runtime][resolver]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto broken = MakeDescriptor("Core.Broken");
    broken.dependencies.push_back(NGIN::Core::DependencyDescriptor {
        .name = "Core.Missing",
        .optional = false,
        .requiredVersion = Range(">=0.1.0"),
    });

    REQUIRE(RegisterModule(
                catalog,
                broken,
                []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Broken", nullptr, nullptr);
                })
                .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("PlatformRangeAndDependencyVersionAreEnforced", "[runtime][compat]")
{
    SECTION("Module platform range mismatch is rejected")
    {
        auto catalog = NGIN::Core::CreateStaticModuleCatalog();
        auto incompatible = MakeDescriptor("Core.Incompatible");
        incompatible.compatiblePlatformRange = Range(">=2.0.0 <3.0.0");
        REQUIRE(RegisterModule(
                    catalog,
                    incompatible,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Incompatible", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::IncompatiblePlatform);
    }

    SECTION("Dependency requiredVersion mismatch is rejected")
    {
        auto catalog = NGIN::Core::CreateStaticModuleCatalog();
        auto dep = MakeDescriptor("Core.Dep");
        dep.version = Version(1, 0, 0);

        auto user = MakeDescriptor("Core.User");
        user.dependencies.push_back(NGIN::Core::DependencyDescriptor {
            .name = "Core.Dep",
            .optional = false,
            .requiredVersion = Range(">=2.0.0"),
        });

        REQUIRE(RegisterModule(
                    catalog,
                    dep,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Dep", nullptr, nullptr);
                    })
                    .HasValue());
        REQUIRE(RegisterModule(
                    catalog,
                    user,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("User", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::IncompatibleVersion);
    }
}

TEST_CASE("LayerAndPhaseViolationsAreRejected", "[runtime][resolver]")
{
    SECTION("Forbidden family dependency fails")
    {
        auto catalog = NGIN::Core::CreateStaticModuleCatalog();
        auto editor = MakeDescriptor("Editor.Tool", NGIN::Core::ModuleFamily::Editor);
        auto app = MakeDescriptor("App.Game", NGIN::Core::ModuleFamily::App);
        app.dependencies.push_back(NGIN::Core::DependencyDescriptor {
            .name = "Editor.Tool",
            .optional = false,
            .requiredVersion = Range(">=0.1.0"),
        });

        REQUIRE(RegisterModule(
                    catalog,
                    editor,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("EditorTool", nullptr, nullptr);
                    })
                    .HasValue());
        REQUIRE(RegisterModule(
                    catalog,
                    app,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("AppGame", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::LayerConstraintViolation);
    }

    SECTION("Dependency with later load phase fails")
    {
        auto catalog = NGIN::Core::CreateStaticModuleCatalog();
        auto early = MakeDescriptor("Core.Early", NGIN::Core::ModuleFamily::Core, NGIN::Core::LoadPhase::Bootstrap);
        auto late = MakeDescriptor("Core.Late", NGIN::Core::ModuleFamily::Core, NGIN::Core::LoadPhase::Application);
        early.dependencies.push_back(NGIN::Core::DependencyDescriptor {
            .name = "Core.Late",
            .optional = false,
            .requiredVersion = Range(">=0.1.0"),
        });

        REQUIRE(RegisterModule(
                    catalog,
                    early,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Early", nullptr, nullptr);
                    })
                    .HasValue());
        REQUIRE(RegisterModule(
                    catalog,
                    late,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Late", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::PhaseOrderingViolation);
    }
}

TEST_CASE("LifecycleFailureUnwindsStartedModules", "[runtime][lifecycle]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    std::vector<std::string> order;
    std::mutex orderLock;

    auto a = MakeDescriptor("Core.A");
    auto b = MakeDescriptor("Core.B");
    b.dependencies.push_back(NGIN::Core::DependencyDescriptor {
        .name = "Core.A",
        .optional = false,
        .requiredVersion = Range(">=0.1.0"),
    });

    REQUIRE(RegisterModule(
                catalog,
                a,
                [&]() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                        "A",
                        &order,
                        &orderLock,
                        HookModule::FailStage::None);
                })
                .HasValue());

    REQUIRE(RegisterModule(
                catalog,
                b,
                [&]() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                        "B",
                        &order,
                        &orderLock,
                        HookModule::FailStage::Init);
                })
                .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::ModuleLifecycleFailure);
    REQUIRE(start.ErrorUnsafe().cause != nullptr);

    REQUIRE(std::find(order.begin(), order.end(), "start:A") == order.end());
    REQUIRE(std::find(order.begin(), order.end(), "shutdown:A") != order.end());
}

TEST_CASE("ServiceRegistrySupportsSingletonScopedTransient", "[runtime][services]")
{
    auto registry = NGIN::Core::CreateServiceRegistry();
    REQUIRE(static_cast<bool>(registry));

    auto scopeA = registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "A");
    auto scopeB = registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "B");
    REQUIRE(scopeA.HasValue());
    REQUIRE(scopeB.HasValue());

    NGIN::UInt32 singletonCounter = 0;
    NGIN::UInt32 scopedCounter = 0;
    NGIN::UInt32 transientCounter = 0;

    REQUIRE(registry->RegisterFactory(
                    "Svc.Singleton",
                    [&]() -> NGIN::Core::CoreResult<NGIN::Utilities::Any<>>
                    {
                        ++singletonCounter;
                        return NGIN::Utilities::Any<>(singletonCounter);
                    },
                    NGIN::Core::ServiceRegistrationOptions {
                        .lifetime = NGIN::Core::ServiceLifetime::Singleton,
                        .ownerScope = NGIN::Core::ServiceScopeId::Global(),
                    })
                    .HasValue());

    REQUIRE(registry->RegisterFactory(
                    "Svc.Scoped",
                    [&]() -> NGIN::Core::CoreResult<NGIN::Utilities::Any<>>
                    {
                        ++scopedCounter;
                        return NGIN::Utilities::Any<>(scopedCounter);
                    },
                    NGIN::Core::ServiceRegistrationOptions {
                        .lifetime = NGIN::Core::ServiceLifetime::Scoped,
                        .ownerScope = scopeA.ValueUnsafe(),
                    })
                    .HasValue());

    REQUIRE(registry->RegisterFactory(
                    "Svc.Transient",
                    [&]() -> NGIN::Core::CoreResult<NGIN::Utilities::Any<>>
                    {
                        ++transientCounter;
                        return NGIN::Utilities::Any<>(transientCounter);
                    },
                    NGIN::Core::ServiceRegistrationOptions {
                        .lifetime = NGIN::Core::ServiceLifetime::Transient,
                        .ownerScope = scopeA.ValueUnsafe(),
                    })
                    .HasValue());

    auto s1 = registry->ResolveRequired("Svc.Singleton");
    auto s2 = registry->ResolveRequired("Svc.Singleton");
    REQUIRE(s1.HasValue());
    REQUIRE(s2.HasValue());
    REQUIRE(*s1.ValueUnsafe().TryCast<NGIN::UInt32>() == 1);
    REQUIRE(*s2.ValueUnsafe().TryCast<NGIN::UInt32>() == 1);

    auto scopedA1 = registry->ResolveRequired("Svc.Scoped", scopeA.ValueUnsafe());
    auto scopedA2 = registry->ResolveRequired("Svc.Scoped", scopeA.ValueUnsafe());
    auto scopedB1 = registry->ResolveRequired("Svc.Scoped", scopeB.ValueUnsafe());
    REQUIRE(scopedA1.HasValue());
    REQUIRE(scopedA2.HasValue());
    REQUIRE(scopedB1.HasValue());
    REQUIRE(*scopedA1.ValueUnsafe().TryCast<NGIN::UInt32>() == *scopedA2.ValueUnsafe().TryCast<NGIN::UInt32>());
    REQUIRE(*scopedB1.ValueUnsafe().TryCast<NGIN::UInt32>() != *scopedA1.ValueUnsafe().TryCast<NGIN::UInt32>());

    auto t1 = registry->ResolveRequired("Svc.Transient", scopeA.ValueUnsafe());
    auto t2 = registry->ResolveRequired("Svc.Transient", scopeA.ValueUnsafe());
    REQUIRE(t1.HasValue());
    REQUIRE(t2.HasValue());
    REQUIRE(*t1.ValueUnsafe().TryCast<NGIN::UInt32>() != *t2.ValueUnsafe().TryCast<NGIN::UInt32>());

    REQUIRE(registry->EndScope(scopeA.ValueUnsafe()).HasValue());
    auto scopedAfterEnd = registry->ResolveOptional("Svc.Scoped", scopeA.ValueUnsafe());
    REQUIRE(scopedAfterEnd.HasValue());
    REQUIRE_FALSE(scopedAfterEnd.ValueUnsafe().has_value());
}

TEST_CASE("ModuleRequiredServiceContractsAreEnforcedBeforeInit", "[runtime][services]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto consumer = MakeDescriptor("Core.Consumer");
    consumer.requiresServices.push_back("Service.Required");

    REQUIRE(RegisterModule(
                catalog,
                consumer,
                []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Consumer", nullptr, nullptr);
                })
                .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::MissingRequiredDependency);
}

TEST_CASE("HostConfigSourcesCliAndEnvironmentAreApplied", "[runtime][config]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto desc = MakeDescriptor("Core.ConfigModule");
    REQUIRE(RegisterModule(
                catalog,
                desc,
                []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("ConfigModule", nullptr, nullptr);
                })
                .HasValue());

    const auto tempRoot = std::filesystem::temp_directory_path() / "ngin-runtime-tests-config";
    std::filesystem::create_directories(tempRoot);
    const auto configPath = tempRoot / "base.cfg";
    {
        std::ofstream out(configPath);
        out << "App.Value=file\n";
        out << "App.Other=from_file\n";
    }

    auto cfg = MakeHostConfig(catalog);
    cfg.workingDirectory = tempRoot.string();
    cfg.environmentName = "TestEnv";
    cfg.configSources = {"base.cfg"};
    cfg.commandLineArgs = {"--App.Value=cli"};

    auto kernel = NGIN::Core::CreateKernel(cfg).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());

    auto config = kernel->GetConfig();
    REQUIRE(static_cast<bool>(config));
    REQUIRE(config->GetRaw("App.Value").ValueUnsafe() == "cli");
    REQUIRE(config->GetRaw("App.Other").ValueUnsafe() == "from_file");
    REQUIRE(config->GetRaw("Kernel.EnvironmentName").ValueUnsafe() == "TestEnv");
    REQUIRE(config->GetRaw("Kernel.HostName").ValueUnsafe() == "Core.Tests");

    REQUIRE(kernel->Shutdown().HasValue());
}

TEST_CASE("DynamicDescriptorDiscoveryUsesPluginSearchPaths", "[runtime][plugin]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    const auto root = std::filesystem::temp_directory_path() / "ngin-runtime-tests-plugins";
    const auto pluginDir = root / "DemoPlugin";
    std::filesystem::create_directories(pluginDir);
    const auto descriptorPath = pluginDir / "demo.module.json";
    {
        std::ofstream out(descriptorPath);
        out << "{\n"
            << "  \"name\": \"Core.DynamicDemo\",\n"
            << "  \"family\": \"Core\",\n"
            << "  \"type\": \"Runtime\",\n"
            << "  \"loadPhase\": \"CoreServices\",\n"
            << "  \"version\": \"0.1.0\",\n"
            << "  \"compatiblePlatformRange\": \">=0.1.0 <1.0.0\",\n"
            << "  \"platforms\": [\"linux\", \"windows\", \"macos\"],\n"
            << "  \"dependencies\": [],\n"
            << "  \"requiresServices\": [],\n"
            << "  \"providesServices\": [],\n"
            << "  \"capabilities\": []\n"
            << "}\n";
    }

    auto cfg = MakeHostConfig(catalog);
    cfg.enableDynamicPlugins = true;
    cfg.pluginSearchPaths = {root.string()};

    auto kernel = NGIN::Core::CreateKernel(cfg).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::DynamicPluginUnsupported);
}

TEST_CASE("TaskLanesAndBarriersExecutePerLane", "[runtime][tasks]")
{
    auto runtime = NGIN::Core::CreateTaskRuntime(2, true);
    REQUIRE(static_cast<bool>(runtime));

    REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Main));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::IO));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Worker));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Background));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Render));

    std::atomic<NGIN::UInt32> hits {0};
    REQUIRE(runtime->Submit(NGIN::Core::TaskLane::IO, [&]() { hits.fetch_add(1, std::memory_order_relaxed); }).HasValue());
    REQUIRE(runtime->Submit(NGIN::Core::TaskLane::Worker, [&]() { hits.fetch_add(1, std::memory_order_relaxed); }).HasValue());
    REQUIRE(runtime->Submit(NGIN::Core::TaskLane::Render, [&]() { hits.fetch_add(1, std::memory_order_relaxed); }).HasValue());

    REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::IO).HasValue());
    REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::Worker).HasValue());
    REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::Render).HasValue());
    REQUIRE(hits.load(std::memory_order_relaxed) == 3);
}

TEST_CASE("EventBusDeferredQueuesFlushByQueue", "[runtime][events]")
{
    auto bus = NGIN::Core::CreateEventBus();
    REQUIRE(static_cast<bool>(bus));

    std::vector<std::string> seen;
    auto sub = bus->Subscribe(
        "Event.IO",
        [&](const NGIN::Core::EventRecord& record) { seen.push_back(record.channel); },
        NGIN::Core::EventScope {.owner = "tests"});
    REQUIRE(sub.HasValue());

    REQUIRE(bus->EnqueueDeferredTo(
                    NGIN::Core::EventQueue::Main,
                    NGIN::Core::EventRecord {.channel = "Event.Main", .payload = NGIN::Utilities::Any<>(NGIN::UInt32 {1})})
                    .HasValue());
    REQUIRE(bus->EnqueueDeferredTo(
                    NGIN::Core::EventQueue::IO,
                    NGIN::Core::EventRecord {.channel = "Event.IO", .payload = NGIN::Utilities::Any<>(NGIN::UInt32 {2})})
                    .HasValue());

    REQUIRE(bus->FlushDeferredFrom(NGIN::Core::EventQueue::IO).HasValue());
    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0] == "Event.IO");
}

TEST_CASE("KernelEmitsReservedLifecycleEvents", "[runtime][events]")
{
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    std::vector<std::string> events;
    std::mutex eventsLock;

    auto desc = MakeDescriptor("Core.EventProbe");
    REQUIRE(RegisterModule(
                catalog,
                desc,
                [&]() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, EventProbeModule>(&events, &eventsLock);
                })
                .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());
    REQUIRE(kernel->Shutdown().HasValue());

    REQUIRE(std::find(events.begin(), events.end(), "ModuleLoaded") != events.end());
    REQUIRE(std::find(events.begin(), events.end(), "ModuleStarted") != events.end());
    REQUIRE(std::find(events.begin(), events.end(), "KernelRunning") != events.end());
    REQUIRE(std::find(events.begin(), events.end(), "KernelStopping") != events.end());
}

TEST_CASE("ThreadPoliciesAreEnforced", "[runtime][threading]")
{
    SECTION("SingleThreadOnly rejects non-owner thread")
    {
        auto catalog = NGIN::Core::CreateStaticModuleCatalog();
        auto cfg = MakeHostConfig(catalog);
        cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::SingleThreadOnly;

        auto kernel = NGIN::Core::CreateKernel(cfg).ValueUnsafe();

        std::promise<NGIN::Core::CoreResult<void>> promise;
        auto future = promise.get_future();
        std::thread worker([&]() mutable { promise.set_value(kernel->Start()); });
        worker.join();

        auto start = future.get();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::ThreadPolicyViolation);
    }

    SECTION("Serialized allows cross-thread run and RequestStop")
    {
        auto catalog = NGIN::Core::CreateStaticModuleCatalog();
        auto desc = MakeDescriptor("Core.Runner");
        REQUIRE(RegisterModule(
                    catalog,
                    desc,
                    []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Runner", nullptr, nullptr);
                    })
                    .HasValue());

        auto cfg = MakeHostConfig(catalog);
        cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::Serialized;
        auto kernel = NGIN::Core::CreateKernel(cfg).ValueUnsafe();

        std::promise<NGIN::Core::CoreResult<void>> promise;
        auto future = promise.get_future();
        std::thread runner([&]() mutable { promise.set_value(kernel->Run()); });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        kernel->RequestStop("test");

        runner.join();
        auto result = future.get();
        REQUIRE(result.HasValue());
        REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Shutdown);
    }
}

TEST_CASE("ApplicationBuilderBuildsHostFromCode", "[builder][host]")
{
    GlobalStaticModuleGuard guard;

    NGIN::Core::ModuleDescriptor descriptor = MakeDescriptor("App.Builder");
    descriptor.family = NGIN::Core::ModuleFamily::App;
    descriptor.loadPhase = NGIN::Core::LoadPhase::Application;
    REQUIRE(NGIN::Core::RegisterStaticModule(
                NGIN::Core::StaticModuleRegistration {
                    .descriptor = descriptor,
                    .factory = []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Builder", nullptr, nullptr);
                    }})
                .HasValue());

    auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
    builder->SetApplicationName("Builder.Tests");
    builder->SetDefaultTarget("Builder.Target");
    builder->UseProfile(NGIN::Core::HostProfile::ConsoleApp);
    builder->Services()
        .AddDefaults()
        .AddConfiguration()
        .AddSingleton("App.Message", NGIN::Utilities::Any<>(std::string("hello-builder")));
    builder->Modules().Enable("App.Builder");

    auto app = builder->Build();
    REQUIRE(app.HasValue());
    REQUIRE(app.ValueUnsafe()->GetProfile() == NGIN::Core::HostProfile::ConsoleApp);
    REQUIRE(app.ValueUnsafe()->GetTargetName() == "Builder.Target");

    auto report = app.ValueUnsafe()->GetStartupReport();
    REQUIRE(report.targetName == "Builder.Target");
    REQUIRE(report.hostName == "Builder.Tests");
    REQUIRE(report.hostType == "ConsoleApp");

    REQUIRE(app.ValueUnsafe()->Start().HasValue());

    auto services = app.ValueUnsafe()->GetServices();
    REQUIRE(static_cast<bool>(services));

    auto resolved = services->ResolveRequired("App.Message");
    REQUIRE(resolved.HasValue());
    REQUIRE(resolved.ValueUnsafe().template TryCast<std::string>() != nullptr);
    REQUIRE(*resolved.ValueUnsafe().template TryCast<std::string>() == "hello-builder");

    auto config = app.ValueUnsafe()->GetConfig();
    REQUIRE(static_cast<bool>(config));
    REQUIRE(config->GetRaw("Kernel.TargetName").HasValue());
    REQUIRE(config->GetRaw("Kernel.TargetName").ValueUnsafe() == "Builder.Target");

    auto secondBuild = builder->Build();
    REQUIRE_FALSE(secondBuild.HasValue());
    REQUIRE(secondBuild.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::InvalidState);

    REQUIRE(app.ValueUnsafe()->Shutdown().HasValue());
}

TEST_CASE("ApplicationBuilderLoadsProjectManifestAndConfig", "[builder][manifest]")
{
    GlobalStaticModuleGuard guard;

    NGIN::Core::ModuleDescriptor descriptor = MakeDescriptor("App.Manifest");
    descriptor.family = NGIN::Core::ModuleFamily::App;
    descriptor.loadPhase = NGIN::Core::LoadPhase::Application;
    REQUIRE(NGIN::Core::RegisterStaticModule(
                NGIN::Core::StaticModuleRegistration {
                    .descriptor = descriptor,
                    .factory = []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Manifest", nullptr, nullptr);
                    }})
                .HasValue());

    NGIN::Core::ModuleDescriptor disabledDescriptor = MakeDescriptor("App.Disabled");
    disabledDescriptor.family = NGIN::Core::ModuleFamily::App;
    disabledDescriptor.loadPhase = NGIN::Core::LoadPhase::Application;
    REQUIRE(NGIN::Core::RegisterStaticModule(
                NGIN::Core::StaticModuleRegistration {
                    .descriptor = disabledDescriptor,
                    .factory = []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Disabled", nullptr, nullptr);
                    }})
                .HasValue());

    const auto uniqueId = std::to_string(
        static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto tempDir = std::filesystem::temp_directory_path() / ("ngin-core-builder-manifest-" + uniqueId);
    std::filesystem::create_directories(tempDir);

    WriteTextFile(tempDir / "app.cfg", "App.Mode=manifest\n");
    WriteTextFile(
        tempDir / "ngin.project.json",
        R"({
  "schemaVersion": 1,
  "name": "Manifest.Tests",
  "defaultTarget": "Samples.Manifest",
  "targets": [
    {
      "name": "Samples.Manifest",
      "type": "Program",
      "profile": "ConsoleApp",
      "platform": "linux-x64",
      "enableReflection": false,
      "packages": [
        {
          "name": "NGIN.ECS",
          "versionRange": ">=0.1.0 <1.0.0"
        }
      ],
      "modules": {
        "enable": [
          "App.Manifest"
        ],
        "disable": [
          "App.Disabled"
        ]
      },
      "plugins": {
        "enable": [
          "Plugin.Sample"
        ],
        "disable": []
      },
      "environmentName": "Dev",
      "configSources": [
        "app.cfg"
      ],
      "workingDirectory": "."
    }
  ]
})");

    auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
    builder->UseProjectFile((tempDir / "ngin.project.json").string());
    builder->Services().AddConfiguration();

    auto app = builder->Build();
    REQUIRE(app.HasValue());
    REQUIRE(app.ValueUnsafe()->Start().HasValue());

    auto report = app.ValueUnsafe()->GetStartupReport();
    REQUIRE(report.targetName == "Samples.Manifest");
    REQUIRE(std::find(report.resolvedPackages.begin(), report.resolvedPackages.end(), "NGIN.ECS") != report.resolvedPackages.end());
    REQUIRE(std::find(report.resolvedPlugins.begin(), report.resolvedPlugins.end(), "Plugin.Sample") != report.resolvedPlugins.end());
    REQUIRE(std::find(report.resolvedModules.begin(), report.resolvedModules.end(), "App.Manifest") != report.resolvedModules.end());
    REQUIRE(std::find(report.resolvedModules.begin(), report.resolvedModules.end(), "App.Disabled") == report.resolvedModules.end());

    auto config = app.ValueUnsafe()->GetConfig();
    REQUIRE(static_cast<bool>(config));
    REQUIRE(config->GetRaw("App.Mode").HasValue());
    REQUIRE(config->GetRaw("App.Mode").ValueUnsafe() == "manifest");
    REQUIRE(config->GetRaw("Kernel.EnvironmentName").HasValue());
    REQUIRE(config->GetRaw("Kernel.EnvironmentName").ValueUnsafe() == "Dev");

    REQUIRE(app.ValueUnsafe()->Shutdown().HasValue());
    std::filesystem::remove_all(tempDir);
}

TEST_CASE("ApplicationBuilderTargetOverrideBeatsProjectDefault", "[builder][manifest]")
{
    GlobalStaticModuleGuard guard;

    NGIN::Core::ModuleDescriptor defaultDescriptor = MakeDescriptor("App.Default");
    defaultDescriptor.family = NGIN::Core::ModuleFamily::App;
    defaultDescriptor.loadPhase = NGIN::Core::LoadPhase::Application;
    REQUIRE(NGIN::Core::RegisterStaticModule(
                NGIN::Core::StaticModuleRegistration {
                    .descriptor = defaultDescriptor,
                    .factory = []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Default", nullptr, nullptr);
                    }})
                .HasValue());

    NGIN::Core::ModuleDescriptor overrideDescriptor = MakeDescriptor("App.Override");
    overrideDescriptor.family = NGIN::Core::ModuleFamily::App;
    overrideDescriptor.loadPhase = NGIN::Core::LoadPhase::Application;
    REQUIRE(NGIN::Core::RegisterStaticModule(
                NGIN::Core::StaticModuleRegistration {
                    .descriptor = overrideDescriptor,
                    .factory = []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>("Override", nullptr, nullptr);
                    }})
                .HasValue());

    const auto uniqueId = std::to_string(
        static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto tempDir = std::filesystem::temp_directory_path() / ("ngin-core-builder-target-" + uniqueId);
    std::filesystem::create_directories(tempDir);

    WriteTextFile(tempDir / "ngin.project.json", R"({
  "schemaVersion": 1,
  "name": "Manifest.Override",
  "defaultTarget": "Default.Target",
  "targets": [
    {
      "name": "Default.Target",
      "type": "Program",
      "profile": "ConsoleApp",
      "platform": "linux-x64",
      "enableReflection": false,
      "packages": [],
      "modules": {
        "enable": [
          "App.Default"
        ],
        "disable": []
      },
      "plugins": {
        "enable": [],
        "disable": []
      },
      "environmentName": "Default",
      "configSources": [],
      "workingDirectory": "."
    },
    {
      "name": "Override.Target",
      "type": "Program",
      "profile": "ConsoleApp",
      "platform": "linux-x64",
      "enableReflection": false,
      "packages": [],
      "modules": {
        "enable": [
          "App.Override"
        ],
        "disable": []
      },
      "plugins": {
        "enable": [],
        "disable": []
      },
      "environmentName": "Override",
      "configSources": [],
      "workingDirectory": "."
    }
  ]
})");

    auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
    builder->UseProjectFile((tempDir / "ngin.project.json").string());
    builder->SetDefaultTarget("Override.Target");

    auto app = builder->Build();
    REQUIRE(app.HasValue());
    REQUIRE(app.ValueUnsafe()->Start().HasValue());

    auto report = app.ValueUnsafe()->GetStartupReport();
    REQUIRE(report.targetName == "Override.Target");
    REQUIRE(std::find(report.resolvedModules.begin(), report.resolvedModules.end(), "App.Override") != report.resolvedModules.end());
    REQUIRE(std::find(report.resolvedModules.begin(), report.resolvedModules.end(), "App.Default") == report.resolvedModules.end());

    auto config = app.ValueUnsafe()->GetConfig();
    REQUIRE(static_cast<bool>(config));
    REQUIRE(config->GetRaw("Kernel.EnvironmentName").HasValue());
    REQUIRE(config->GetRaw("Kernel.EnvironmentName").ValueUnsafe() == "Override");

    REQUIRE(app.ValueUnsafe()->Shutdown().HasValue());
    std::filesystem::remove_all(tempDir);
}

TEST_CASE("ApplicationBuilderRejectsUnknownTarget", "[builder][manifest]")
{
    const auto uniqueId = std::to_string(
        static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto tempDir = std::filesystem::temp_directory_path() / ("ngin-core-builder-missing-target-" + uniqueId);
    std::filesystem::create_directories(tempDir);

    WriteTextFile(tempDir / "ngin.project.json", R"({
  "schemaVersion": 1,
  "name": "Manifest.Invalid",
  "defaultTarget": "Samples.Default",
  "targets": [
    {
      "name": "Samples.Default",
      "type": "Program",
      "profile": "ConsoleApp",
      "platform": "linux-x64",
      "enableReflection": false,
      "packages": [],
      "modules": {
        "enable": [],
        "disable": []
      },
      "plugins": {
        "enable": [],
        "disable": []
      },
      "environmentName": "",
      "configSources": [],
      "workingDirectory": "."
    }
  ]
})");

    auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
    builder->UseProjectFile((tempDir / "ngin.project.json").string());
    builder->SetDefaultTarget("Missing.Target");

    auto app = builder->Build();
    REQUIRE_FALSE(app.HasValue());
    REQUIRE(app.ErrorUnsafe().code == NGIN::Core::KernelErrorCode::NotFound);

    std::filesystem::remove_all(tempDir);
}
