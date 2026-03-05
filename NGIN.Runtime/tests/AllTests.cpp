#include <catch2/catch_test_macros.hpp>

#include <NGIN/Runtime/Runtime.hpp>

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
    [[nodiscard]] auto Range(const std::string_view text) -> NGIN::Runtime::VersionRange
    {
        auto parsed = NGIN::Runtime::ParseVersionRange(text);
        REQUIRE(parsed.HasValue());
        return parsed.ValueUnsafe();
    }

    [[nodiscard]] auto Version(
        const NGIN::UInt32 major,
        const NGIN::UInt32 minor,
        const NGIN::UInt32 patch) -> NGIN::Runtime::SemanticVersion
    {
        return NGIN::Runtime::SemanticVersion {major, minor, patch, {}};
    }

    [[nodiscard]] auto MakeDescriptor(
        std::string name,
        const NGIN::Runtime::ModuleFamily family = NGIN::Runtime::ModuleFamily::RuntimeSvc,
        const NGIN::Runtime::LoadPhase phase = NGIN::Runtime::LoadPhase::CoreServices,
        const NGIN::Runtime::ModuleType type = NGIN::Runtime::ModuleType::Runtime) -> NGIN::Runtime::ModuleDescriptor
    {
        NGIN::Runtime::ModuleDescriptor desc {};
        desc.name = std::move(name);
        desc.family = family;
        desc.type = type;
        desc.version = Version(0, 1, 0);
        desc.compatiblePlatformRange = Range(">=0.1.0 <1.0.0");
        desc.platforms = {"linux", "windows", "macos"};
        desc.loadPhase = phase;
        desc.entryKind = NGIN::Runtime::ModuleEntryKind::Static;
        return desc;
    }

    [[nodiscard]] auto MakeHostConfig(
        const NGIN::Memory::Shared<NGIN::Runtime::IModuleCatalog>& catalog) -> NGIN::Runtime::KernelHostConfig
    {
        NGIN::Runtime::KernelHostConfig cfg {};
        cfg.hostName = "Runtime.Tests";
        cfg.hostType = NGIN::Runtime::HostType::Program;
        cfg.platformName = "linux-x64";
        cfg.platformVersion = Version(0, 1, 0);
        cfg.targetName = "Runtime.Tests.Target";
        cfg.enableDynamicPlugins = false;
        cfg.enableReflection = false;
        cfg.logSinkConfig.includeConsoleSink = false;
        cfg.moduleCatalog = catalog;
        cfg.apiThreadPolicy = NGIN::Runtime::KernelApiThreadPolicy::Serialized;
        return cfg;
    }

    auto RegisterModule(
        const NGIN::Memory::Shared<NGIN::Runtime::IModuleCatalog>& catalog,
        NGIN::Runtime::ModuleDescriptor descriptor,
        NGIN::Runtime::ModuleFactory factory) -> NGIN::Runtime::RuntimeResult<void>
    {
        return catalog->Register(NGIN::Runtime::StaticModuleRegistration {
            .descriptor = std::move(descriptor),
            .factory = std::move(factory),
        });
    }

    class HookModule final : public NGIN::Runtime::IModule
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

        auto OnRegister(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            Push("register:" + m_name);
            if (m_failStage == FailStage::Register)
            {
                return NGIN::Utilities::Unexpected<NGIN::Runtime::KernelError>(
                    NGIN::Runtime::MakeKernelError(
                        NGIN::Runtime::KernelErrorCode::ModuleLifecycleFailure,
                        "Test",
                        m_name,
                        "forced register failure"));
            }
            return {};
        }

        auto OnInit(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            Push("init:" + m_name);
            if (m_failStage == FailStage::Init)
            {
                return NGIN::Utilities::Unexpected<NGIN::Runtime::KernelError>(
                    NGIN::Runtime::MakeKernelError(
                        NGIN::Runtime::KernelErrorCode::ModuleLifecycleFailure,
                        "Test",
                        m_name,
                        "forced init failure"));
            }
            return {};
        }

        auto OnStart(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            Push("start:" + m_name);
            if (m_failStage == FailStage::Start)
            {
                return NGIN::Utilities::Unexpected<NGIN::Runtime::KernelError>(
                    NGIN::Runtime::MakeKernelError(
                        NGIN::Runtime::KernelErrorCode::ModuleLifecycleFailure,
                        "Test",
                        m_name,
                        "forced start failure"));
            }
            return {};
        }

        auto OnStop(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            Push("stop:" + m_name);
            return {};
        }

        auto OnShutdown(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
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

    class ScopedServiceModule final : public NGIN::Runtime::IModule
    {
    public:
        explicit ScopedServiceModule(std::string key)
            : m_key(std::move(key))
        {
        }

        auto OnRegister(NGIN::Runtime::ModuleContext& context) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            return context.RegisterFactory(
                m_key,
                []() -> NGIN::Runtime::RuntimeResult<NGIN::Utilities::Any<>>
                {
                    return NGIN::Utilities::Any<>(NGIN::UInt32 {42});
                },
                NGIN::Runtime::ServiceLifetime::Scoped);
        }

    private:
        std::string m_key;
    };

    class ServiceProviderModule final : public NGIN::Runtime::IModule
    {
    public:
        explicit ServiceProviderModule(std::string key)
            : m_key(std::move(key))
        {
        }

        auto OnRegister(NGIN::Runtime::ModuleContext& context) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            return context.RegisterSingleton(m_key, NGIN::Utilities::Any<>(std::string("provided")));
        }

    private:
        std::string m_key;
    };

    class EventProbeModule final : public NGIN::Runtime::IModule
    {
    public:
        EventProbeModule(std::vector<std::string>* events, std::mutex* lock)
            : m_events(events), m_lock(lock)
        {
        }

        auto OnRegister(NGIN::Runtime::ModuleContext& context) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            for (const auto channel : {"ModuleLoaded", "ModuleStarted", "KernelRunning", "KernelStopping"})
            {
                auto sub = context.Events().Subscribe(
                    channel,
                    [this](const NGIN::Runtime::EventRecord& eventRecord) {
                        if (m_events == nullptr || m_lock == nullptr)
                        {
                            return;
                        }
                        std::lock_guard<std::mutex> guard(*m_lock);
                        m_events->push_back(eventRecord.channel);
                    },
                    NGIN::Runtime::EventScope {.owner = std::string(context.ModuleName())});
                if (!sub)
                {
                    return NGIN::Utilities::Unexpected<NGIN::Runtime::KernelError>(sub.ErrorUnsafe());
                }
                m_tokens.push_back(sub.ValueUnsafe());
            }

            return {};
        }

    private:
        std::vector<std::string>*                       m_events {nullptr};
        std::mutex*                                     m_lock {nullptr};
        std::vector<NGIN::Runtime::EventSubscriptionToken> m_tokens {};
    };
}

TEST_CASE("KernelStartsWithDependencyOrderedModules", "[runtime][startup]")
{
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    std::vector<std::string> order;
    std::mutex orderLock;

    auto a = MakeDescriptor("Runtime.A", NGIN::Runtime::ModuleFamily::RuntimeSvc, NGIN::Runtime::LoadPhase::Bootstrap);
    auto b = MakeDescriptor("Runtime.B");
    b.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {
        .name = "Runtime.A",
        .optional = false,
        .requiredVersion = Range(">=0.1.0 <1.0.0"),
    });

    REQUIRE(RegisterModule(
                catalog,
                a,
                [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("A", &order, &orderLock);
                })
                .HasValue());
    REQUIRE(RegisterModule(
                catalog,
                b,
                [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("B", &order, &orderLock);
                })
                .HasValue());

    auto kernelResult = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog));
    REQUIRE(kernelResult.HasValue());
    auto kernel = kernelResult.ValueUnsafe();

    REQUIRE(kernel->Start().HasValue());
    REQUIRE(kernel->GetState() == NGIN::Runtime::KernelState::Running);

    const auto report = kernel->GetStartupReport();
    REQUIRE(report.resolvedModules.size() == 2);
    REQUIRE(report.failures.empty());

    REQUIRE(kernel->Shutdown().HasValue());
    REQUIRE(kernel->GetState() == NGIN::Runtime::KernelState::Shutdown);

    const auto startA = std::find(order.begin(), order.end(), "start:A");
    const auto startB = std::find(order.begin(), order.end(), "start:B");
    REQUIRE(startA != order.end());
    REQUIRE(startB != order.end());
    REQUIRE(startA < startB);
}

TEST_CASE("MissingRequiredDependencyFailsBeforeLifecycle", "[runtime][resolver]")
{
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto broken = MakeDescriptor("Runtime.Broken");
    broken.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {
        .name = "Runtime.Missing",
        .optional = false,
        .requiredVersion = Range(">=0.1.0"),
    });

    REQUIRE(RegisterModule(
                catalog,
                broken,
                []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Broken", nullptr, nullptr);
                })
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::NotFound);
}

TEST_CASE("PlatformRangeAndDependencyVersionAreEnforced", "[runtime][compat]")
{
    SECTION("Module platform range mismatch is rejected")
    {
        auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
        auto incompatible = MakeDescriptor("Runtime.Incompatible");
        incompatible.compatiblePlatformRange = Range(">=2.0.0 <3.0.0");
        REQUIRE(RegisterModule(
                    catalog,
                    incompatible,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Incompatible", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::IncompatiblePlatform);
    }

    SECTION("Dependency requiredVersion mismatch is rejected")
    {
        auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
        auto dep = MakeDescriptor("Runtime.Dep");
        dep.version = Version(1, 0, 0);

        auto user = MakeDescriptor("Runtime.User");
        user.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {
            .name = "Runtime.Dep",
            .optional = false,
            .requiredVersion = Range(">=2.0.0"),
        });

        REQUIRE(RegisterModule(
                    catalog,
                    dep,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Dep", nullptr, nullptr);
                    })
                    .HasValue());
        REQUIRE(RegisterModule(
                    catalog,
                    user,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("User", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::IncompatibleVersion);
    }
}

TEST_CASE("LayerAndPhaseViolationsAreRejected", "[runtime][resolver]")
{
    SECTION("Forbidden family dependency fails")
    {
        auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
        auto editor = MakeDescriptor("Editor.Tool", NGIN::Runtime::ModuleFamily::Editor);
        auto app = MakeDescriptor("App.Game", NGIN::Runtime::ModuleFamily::App);
        app.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {
            .name = "Editor.Tool",
            .optional = false,
            .requiredVersion = Range(">=0.1.0"),
        });

        REQUIRE(RegisterModule(
                    catalog,
                    editor,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("EditorTool", nullptr, nullptr);
                    })
                    .HasValue());
        REQUIRE(RegisterModule(
                    catalog,
                    app,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("AppGame", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::LayerConstraintViolation);
    }

    SECTION("Dependency with later load phase fails")
    {
        auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
        auto early = MakeDescriptor("Runtime.Early", NGIN::Runtime::ModuleFamily::RuntimeSvc, NGIN::Runtime::LoadPhase::Bootstrap);
        auto late = MakeDescriptor("Runtime.Late", NGIN::Runtime::ModuleFamily::RuntimeSvc, NGIN::Runtime::LoadPhase::Application);
        early.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {
            .name = "Runtime.Late",
            .optional = false,
            .requiredVersion = Range(">=0.1.0"),
        });

        REQUIRE(RegisterModule(
                    catalog,
                    early,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Early", nullptr, nullptr);
                    })
                    .HasValue());
        REQUIRE(RegisterModule(
                    catalog,
                    late,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Late", nullptr, nullptr);
                    })
                    .HasValue());

        auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
        auto start = kernel->Start();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::PhaseOrderingViolation);
    }
}

TEST_CASE("LifecycleFailureUnwindsStartedModules", "[runtime][lifecycle]")
{
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    std::vector<std::string> order;
    std::mutex orderLock;

    auto a = MakeDescriptor("Runtime.A");
    auto b = MakeDescriptor("Runtime.B");
    b.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {
        .name = "Runtime.A",
        .optional = false,
        .requiredVersion = Range(">=0.1.0"),
    });

    REQUIRE(RegisterModule(
                catalog,
                a,
                [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>(
                        "A",
                        &order,
                        &orderLock,
                        HookModule::FailStage::None);
                })
                .HasValue());

    REQUIRE(RegisterModule(
                catalog,
                b,
                [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>(
                        "B",
                        &order,
                        &orderLock,
                        HookModule::FailStage::Init);
                })
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::ModuleLifecycleFailure);
    REQUIRE(start.ErrorUnsafe().cause != nullptr);

    REQUIRE(std::find(order.begin(), order.end(), "start:A") == order.end());
    REQUIRE(std::find(order.begin(), order.end(), "shutdown:A") != order.end());
}

TEST_CASE("ServiceRegistrySupportsSingletonScopedTransient", "[runtime][services]")
{
    auto registry = NGIN::Runtime::CreateServiceRegistry();
    REQUIRE(static_cast<bool>(registry));

    auto scopeA = registry->BeginScope(NGIN::Runtime::ServiceScopeKind::Module, "A");
    auto scopeB = registry->BeginScope(NGIN::Runtime::ServiceScopeKind::Module, "B");
    REQUIRE(scopeA.HasValue());
    REQUIRE(scopeB.HasValue());

    NGIN::UInt32 singletonCounter = 0;
    NGIN::UInt32 scopedCounter = 0;
    NGIN::UInt32 transientCounter = 0;

    REQUIRE(registry->RegisterFactory(
                    "Svc.Singleton",
                    [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Utilities::Any<>>
                    {
                        ++singletonCounter;
                        return NGIN::Utilities::Any<>(singletonCounter);
                    },
                    NGIN::Runtime::ServiceRegistrationOptions {
                        .lifetime = NGIN::Runtime::ServiceLifetime::Singleton,
                        .ownerScope = NGIN::Runtime::ServiceScopeId::Global(),
                    })
                    .HasValue());

    REQUIRE(registry->RegisterFactory(
                    "Svc.Scoped",
                    [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Utilities::Any<>>
                    {
                        ++scopedCounter;
                        return NGIN::Utilities::Any<>(scopedCounter);
                    },
                    NGIN::Runtime::ServiceRegistrationOptions {
                        .lifetime = NGIN::Runtime::ServiceLifetime::Scoped,
                        .ownerScope = scopeA.ValueUnsafe(),
                    })
                    .HasValue());

    REQUIRE(registry->RegisterFactory(
                    "Svc.Transient",
                    [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Utilities::Any<>>
                    {
                        ++transientCounter;
                        return NGIN::Utilities::Any<>(transientCounter);
                    },
                    NGIN::Runtime::ServiceRegistrationOptions {
                        .lifetime = NGIN::Runtime::ServiceLifetime::Transient,
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
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto consumer = MakeDescriptor("Runtime.Consumer");
    consumer.requiresServices.push_back("Service.Required");

    REQUIRE(RegisterModule(
                catalog,
                consumer,
                []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Consumer", nullptr, nullptr);
                })
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::MissingRequiredDependency);
}

TEST_CASE("HostConfigSourcesCliAndEnvironmentAreApplied", "[runtime][config]")
{
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    auto desc = MakeDescriptor("Runtime.ConfigModule");
    REQUIRE(RegisterModule(
                catalog,
                desc,
                []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("ConfigModule", nullptr, nullptr);
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

    auto kernel = NGIN::Runtime::CreateKernel(cfg).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());

    auto config = kernel->GetConfig();
    REQUIRE(static_cast<bool>(config));
    REQUIRE(config->GetRaw("App.Value").ValueUnsafe() == "cli");
    REQUIRE(config->GetRaw("App.Other").ValueUnsafe() == "from_file");
    REQUIRE(config->GetRaw("Kernel.EnvironmentName").ValueUnsafe() == "TestEnv");
    REQUIRE(config->GetRaw("Kernel.HostName").ValueUnsafe() == "Runtime.Tests");

    REQUIRE(kernel->Shutdown().HasValue());
}

TEST_CASE("DynamicDescriptorDiscoveryUsesPluginSearchPaths", "[runtime][plugin]")
{
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    const auto root = std::filesystem::temp_directory_path() / "ngin-runtime-tests-plugins";
    const auto pluginDir = root / "DemoPlugin";
    std::filesystem::create_directories(pluginDir);
    const auto descriptorPath = pluginDir / "demo.module.json";
    {
        std::ofstream out(descriptorPath);
        out << "{\n"
            << "  \"name\": \"Runtime.DynamicDemo\",\n"
            << "  \"family\": \"RuntimeSvc\",\n"
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

    auto kernel = NGIN::Runtime::CreateKernel(cfg).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::DynamicPluginUnsupported);
}

TEST_CASE("TaskLanesAndBarriersExecutePerLane", "[runtime][tasks]")
{
    auto runtime = NGIN::Runtime::CreateTaskRuntime(2, true);
    REQUIRE(static_cast<bool>(runtime));

    REQUIRE(runtime->IsLaneEnabled(NGIN::Runtime::TaskLane::Main));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Runtime::TaskLane::IO));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Runtime::TaskLane::Worker));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Runtime::TaskLane::Background));
    REQUIRE(runtime->IsLaneEnabled(NGIN::Runtime::TaskLane::Render));

    std::atomic<NGIN::UInt32> hits {0};
    REQUIRE(runtime->Submit(NGIN::Runtime::TaskLane::IO, [&]() { hits.fetch_add(1, std::memory_order_relaxed); }).HasValue());
    REQUIRE(runtime->Submit(NGIN::Runtime::TaskLane::Worker, [&]() { hits.fetch_add(1, std::memory_order_relaxed); }).HasValue());
    REQUIRE(runtime->Submit(NGIN::Runtime::TaskLane::Render, [&]() { hits.fetch_add(1, std::memory_order_relaxed); }).HasValue());

    REQUIRE(runtime->Barrier(NGIN::Runtime::TaskLane::IO).HasValue());
    REQUIRE(runtime->Barrier(NGIN::Runtime::TaskLane::Worker).HasValue());
    REQUIRE(runtime->Barrier(NGIN::Runtime::TaskLane::Render).HasValue());
    REQUIRE(hits.load(std::memory_order_relaxed) == 3);
}

TEST_CASE("EventBusDeferredQueuesFlushByQueue", "[runtime][events]")
{
    auto bus = NGIN::Runtime::CreateEventBus();
    REQUIRE(static_cast<bool>(bus));

    std::vector<std::string> seen;
    auto sub = bus->Subscribe(
        "Event.IO",
        [&](const NGIN::Runtime::EventRecord& record) { seen.push_back(record.channel); },
        NGIN::Runtime::EventScope {.owner = "tests"});
    REQUIRE(sub.HasValue());

    REQUIRE(bus->EnqueueDeferredTo(
                    NGIN::Runtime::EventQueue::Main,
                    NGIN::Runtime::EventRecord {.channel = "Event.Main", .payload = NGIN::Utilities::Any<>(NGIN::UInt32 {1})})
                    .HasValue());
    REQUIRE(bus->EnqueueDeferredTo(
                    NGIN::Runtime::EventQueue::IO,
                    NGIN::Runtime::EventRecord {.channel = "Event.IO", .payload = NGIN::Utilities::Any<>(NGIN::UInt32 {2})})
                    .HasValue());

    REQUIRE(bus->FlushDeferredFrom(NGIN::Runtime::EventQueue::IO).HasValue());
    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0] == "Event.IO");
}

TEST_CASE("KernelEmitsReservedLifecycleEvents", "[runtime][events]")
{
    auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    std::vector<std::string> events;
    std::mutex eventsLock;

    auto desc = MakeDescriptor("Runtime.EventProbe");
    REQUIRE(RegisterModule(
                catalog,
                desc,
                [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, EventProbeModule>(&events, &eventsLock);
                })
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig(catalog)).ValueUnsafe();
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
        auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
        auto cfg = MakeHostConfig(catalog);
        cfg.apiThreadPolicy = NGIN::Runtime::KernelApiThreadPolicy::SingleThreadOnly;

        auto kernel = NGIN::Runtime::CreateKernel(cfg).ValueUnsafe();

        std::promise<NGIN::Runtime::RuntimeResult<void>> promise;
        auto future = promise.get_future();
        std::thread worker([&]() mutable { promise.set_value(kernel->Start()); });
        worker.join();

        auto start = future.get();
        REQUIRE_FALSE(start.HasValue());
        REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::ThreadPolicyViolation);
    }

    SECTION("Serialized allows cross-thread run and RequestStop")
    {
        auto catalog = NGIN::Runtime::CreateStaticModuleCatalog();
        auto desc = MakeDescriptor("Runtime.Runner");
        REQUIRE(RegisterModule(
                    catalog,
                    desc,
                    []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>>
                    {
                        return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Runner", nullptr, nullptr);
                    })
                    .HasValue());

        auto cfg = MakeHostConfig(catalog);
        cfg.apiThreadPolicy = NGIN::Runtime::KernelApiThreadPolicy::Serialized;
        auto kernel = NGIN::Runtime::CreateKernel(cfg).ValueUnsafe();

        std::promise<NGIN::Runtime::RuntimeResult<void>> promise;
        auto future = promise.get_future();
        std::thread runner([&]() mutable { promise.set_value(kernel->Run()); });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        kernel->RequestStop("test");

        runner.join();
        auto result = future.get();
        REQUIRE(result.HasValue());
        REQUIRE(kernel->GetState() == NGIN::Runtime::KernelState::Shutdown);
    }
}
