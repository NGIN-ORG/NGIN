#include <catch2/catch_test_macros.hpp>

#include <NGIN/Runtime/Runtime.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    struct StaticRegistryScope final
    {
        StaticRegistryScope() { NGIN::Runtime::ClearStaticModules(); }
        ~StaticRegistryScope() { NGIN::Runtime::ClearStaticModules(); }
    };

    [[nodiscard]] auto MakeDescriptor(
        std::string name,
        NGIN::Runtime::LoadPhase phase = NGIN::Runtime::LoadPhase::CoreServices,
        NGIN::Runtime::ModuleType type = NGIN::Runtime::ModuleType::Runtime) -> NGIN::Runtime::ModuleDescriptor
    {
        NGIN::Runtime::ModuleDescriptor desc {};
        desc.name = std::move(name);
        desc.type = type;
        desc.version = NGIN::Runtime::SemanticVersion {0, 1, 0, {}};
        desc.compatiblePlatformRange = NGIN::Runtime::VersionRange {};
        desc.platforms = {"linux", "windows", "macos"};
        desc.loadPhase = phase;
        desc.entryKind = NGIN::Runtime::ModuleEntryKind::Static;
        return desc;
    }

    class HookModule final : public NGIN::Runtime::IModule
    {
    public:
        HookModule(
            std::string name,
            std::vector<std::string>* order,
            std::mutex* lock,
            bool failInit = false)
            : m_name(std::move(name))
            , m_order(order)
            , m_lock(lock)
            , m_failInit(failInit)
        {
        }

        auto OnRegister(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            Push("register:" + m_name);
            return {};
        }

        auto OnInit(NGIN::Runtime::ModuleContext&) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            Push("init:" + m_name);
            if (m_failInit)
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
            if (!m_order || !m_lock)
            {
                return;
            }
            std::lock_guard<std::mutex> guard(*m_lock);
            m_order->push_back(value);
        }

        std::string               m_name;
        std::vector<std::string>* m_order {nullptr};
        std::mutex*               m_lock {nullptr};
        bool                      m_failInit {false};
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
            return context.Services().RegisterInstance(
                m_key,
                NGIN::Utilities::Any<>(NGIN::UInt32 {42}),
                NGIN::Runtime::ServiceScope {
                    .lifetime = NGIN::Runtime::ServiceLifetime::Module,
                    .owner = std::string(context.ModuleName())},
                {});
        }

    private:
        std::string m_key;
    };

    class DynamicCatalog final : public NGIN::Runtime::IPluginCatalog
    {
    public:
        auto CollectDescriptors(std::vector<NGIN::Runtime::ModuleDescriptor>& out) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            auto desc = MakeDescriptor("Runtime.DynamicPlaceholder");
            desc.entryKind = NGIN::Runtime::ModuleEntryKind::Dynamic;
            out.push_back(std::move(desc));
            return {};
        }
    };

    [[nodiscard]] auto MakeHostConfig() -> NGIN::Runtime::KernelHostConfig
    {
        NGIN::Runtime::KernelHostConfig cfg {};
        cfg.hostName = "Runtime.Tests";
        cfg.hostType = NGIN::Runtime::HostType::Program;
        cfg.platformName = "linux-x64";
        cfg.targetName = "Runtime.Tests.Target";
        cfg.enableDynamicPlugins = false;
        cfg.enableReflection = false;
        cfg.logSinkConfig.includeConsoleSink = false;
        return cfg;
    }
}

TEST_CASE("KernelStartsWithStaticModules", "[runtime][startup]")
{
    StaticRegistryScope scope;

    std::vector<std::string> order;
    std::mutex orderLock;

    auto a = MakeDescriptor("Runtime.A", NGIN::Runtime::LoadPhase::Bootstrap);
    auto b = MakeDescriptor("Runtime.B", NGIN::Runtime::LoadPhase::CoreServices);
    b.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {.name = "Runtime.A", .optional = false});

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = a,
                .factory = [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("A", &order, &orderLock);
                }})
                .HasValue());

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = b,
                .factory = [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("B", &order, &orderLock);
                }})
                .HasValue());

    auto cfg = MakeHostConfig();
    auto kernelResult = NGIN::Runtime::CreateKernel(cfg);
    REQUIRE(kernelResult.HasValue());

    auto kernel = kernelResult.ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE(start.HasValue());
    REQUIRE(kernel->GetState() == NGIN::Runtime::KernelState::Running);

    auto report = kernel->GetStartupReport();
    REQUIRE(report.resolvedModules.size() == 2);

    auto shutdown = kernel->Shutdown();
    REQUIRE(shutdown.HasValue());
    REQUIRE(kernel->GetState() == NGIN::Runtime::KernelState::Shutdown);

    REQUIRE(std::find(order.begin(), order.end(), "start:A") != order.end());
    REQUIRE(std::find(order.begin(), order.end(), "start:B") != order.end());
}

TEST_CASE("MissingRequiredDependencyFailsBeforeStart", "[runtime][startup]")
{
    StaticRegistryScope scope;

    auto broken = MakeDescriptor("Runtime.Broken");
    broken.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {.name = "Runtime.Missing", .optional = false});

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = broken,
                .factory = []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("Broken", nullptr, nullptr);
                }})
                .HasValue());

    auto kernelResult = NGIN::Runtime::CreateKernel(MakeHostConfig());
    REQUIRE(kernelResult.HasValue());

    auto start = kernelResult.ValueUnsafe()->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE((
        start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::NotFound
        || start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::MissingRequiredDependency));
}

TEST_CASE("OptionalDependencyMissingStillStarts", "[runtime][startup]")
{
    StaticRegistryScope scope;

    auto desc = MakeDescriptor("Runtime.OptionalUser");
    desc.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {.name = "Runtime.NotPresent", .optional = true});

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = desc,
                .factory = []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("OptionalUser", nullptr, nullptr);
                }})
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig()).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE(start.HasValue());

    auto report = kernel->GetStartupReport();
    REQUIRE_FALSE(report.warnings.empty());

    REQUIRE(kernel->Shutdown().HasValue());
}

TEST_CASE("ReflectionRequiredModuleFailsWhenDisabled", "[runtime][compat]")
{
    StaticRegistryScope scope;

    auto desc = MakeDescriptor("Runtime.ReflectionOnly");
    desc.reflectionRequired = true;

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = desc,
                .factory = []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("ReflectionOnly", nullptr, nullptr);
                }})
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig()).ValueUnsafe();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
}

TEST_CASE("ShutdownOrderIsReverseDependencyOrder", "[runtime][lifecycle]")
{
    StaticRegistryScope scope;

    std::vector<std::string> order;
    std::mutex orderLock;

    auto a = MakeDescriptor("Runtime.A", NGIN::Runtime::LoadPhase::Bootstrap);
    auto b = MakeDescriptor("Runtime.B", NGIN::Runtime::LoadPhase::CoreServices);
    b.dependencies.push_back(NGIN::Runtime::DependencyDescriptor {.name = "Runtime.A", .optional = false});

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = a,
                .factory = [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("A", &order, &orderLock);
                }})
                .HasValue());

    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = b,
                .factory = [&]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("B", &order, &orderLock);
                }})
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig()).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());
    REQUIRE(kernel->Shutdown().HasValue());

    auto stopB = std::find(order.begin(), order.end(), "stop:B");
    auto stopA = std::find(order.begin(), order.end(), "stop:A");
    REQUIRE(stopB != order.end());
    REQUIRE(stopA != order.end());
    REQUIRE(stopB < stopA);
}

TEST_CASE("ModuleScopedServicesAreRemovedOnShutdown", "[runtime][services]")
{
    StaticRegistryScope scope;

    constexpr auto serviceKey = "Runtime.Test.Service";

    auto desc = MakeDescriptor("Runtime.ServiceModule");
    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = desc,
                .factory = [serviceKey]() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, ScopedServiceModule>(serviceKey);
                }})
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig()).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());

    auto resolved = kernel->GetServices()->ResolveOptional(serviceKey);
    REQUIRE(resolved.HasValue());
    REQUIRE(resolved.ValueUnsafe().has_value());

    REQUIRE(kernel->Shutdown().HasValue());

    auto after = kernel->GetServices()->ResolveOptional(serviceKey);
    REQUIRE(after.HasValue());
    REQUIRE_FALSE(after.ValueUnsafe().has_value());
}

TEST_CASE("DeferredEventsFlushOnTick", "[runtime][events]")
{
    StaticRegistryScope scope;

    auto desc = MakeDescriptor("Runtime.EventDriver");
    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = desc,
                .factory = []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("EventDriver", nullptr, nullptr);
                }})
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig()).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());

    std::atomic<bool> seen {false};
    auto sub = kernel->GetEvents()->Subscribe(
        "Runtime.Test.Event",
        [&](const NGIN::Runtime::EventRecord&) { seen.store(true, std::memory_order_release); },
        {.owner = "Tests"});
    REQUIRE(sub.HasValue());

    auto enqueue = kernel->GetEvents()->EnqueueDeferred({.channel = "Runtime.Test.Event", .payload = NGIN::Utilities::Any<>(NGIN::UInt32 {1})});
    REQUIRE(enqueue.HasValue());

    REQUIRE(kernel->Tick().HasValue());
    REQUIRE(seen.load(std::memory_order_acquire));

    REQUIRE(kernel->Shutdown().HasValue());
}

TEST_CASE("ConfigLayerPrecedenceIsDeterministic", "[runtime][config]")
{
    StaticRegistryScope scope;

    auto desc = MakeDescriptor("Runtime.ConfigDriver");
    REQUIRE(NGIN::Runtime::RegisterStaticModule({
                .descriptor = desc,
                .factory = []() -> NGIN::Runtime::RuntimeResult<NGIN::Memory::Shared<NGIN::Runtime::IModule>> {
                    return NGIN::Memory::MakeSharedAs<NGIN::Runtime::IModule, HookModule>("ConfigDriver", nullptr, nullptr);
                }})
                .HasValue());

    auto kernel = NGIN::Runtime::CreateKernel(MakeHostConfig()).ValueUnsafe();
    REQUIRE(kernel->Start().HasValue());

    auto config = kernel->GetConfig();
    REQUIRE(config->SetValue(NGIN::Runtime::ConfigLayer::BuiltInDefaults, "App.Value", "1").HasValue());
    REQUIRE(config->SetValue(NGIN::Runtime::ConfigLayer::RuntimeMutable, "App.Value", "2").HasValue());

    auto raw = config->GetRaw("App.Value");
    REQUIRE(raw.HasValue());
    REQUIRE(raw.ValueUnsafe() == "2");

    auto provenance = config->GetProvenance("App.Value");
    REQUIRE(provenance.HasValue());
    REQUIRE(provenance.ValueUnsafe() == NGIN::Runtime::ConfigLayer::RuntimeMutable);

    REQUIRE(kernel->Shutdown().HasValue());
}

TEST_CASE("DynamicSeamWithoutBinaryLoaderReturnsStructuredError", "[runtime][plugin]")
{
    StaticRegistryScope scope;

    NGIN::Runtime::KernelHostConfig cfg = MakeHostConfig();
    cfg.enableDynamicPlugins = true;
    cfg.pluginCatalog = NGIN::Memory::MakeSharedAs<NGIN::Runtime::IPluginCatalog, DynamicCatalog>();

    auto kernelResult = NGIN::Runtime::CreateKernel(cfg);
    REQUIRE(kernelResult.HasValue());

    auto start = kernelResult.ValueUnsafe()->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.ErrorUnsafe().code == NGIN::Runtime::KernelErrorCode::DynamicPluginUnsupported);
}
