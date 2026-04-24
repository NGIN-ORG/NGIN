#include <NGIN/Core/Core.hpp>

#include <iostream>

namespace
{
    [[nodiscard]] auto MakeDescriptor() -> NGIN::Core::ModuleDescriptor
    {
        using namespace NGIN::Core;

        ModuleDescriptor descriptor {};
        descriptor.name = "App.HostedCore.Runtime";
        descriptor.family = ModuleFamily::App;
        descriptor.type = ModuleType::Runtime;
        descriptor.version = SemanticVersion {0, 1, 0, {}};
        descriptor.compatiblePlatformRange = ParseVersionRange(">=0.1.0 <1.0.0").Value();
        descriptor.operatingSystems = {"linux", "windows", "macos"};
        descriptor.startupStage = StartupStage::Features;
        descriptor.entryKind = ModuleEntryKind::Static;
        return descriptor;
    }

    class HostedCoreModule final : public NGIN::Core::IModule
    {
    public:
        auto OnStart(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            auto logger = context.GetLogger("Startup");
            if (logger)
            {
                logger->Info([](NGIN::Log::RecordBuilder& rec) {
                    rec.Message("App.HostedCore runtime module started");
                });
            }
            return context.RegisterSingleton("App.HostedCore.Ready", NGIN::Utilities::Any<>(true));
        }
    };
}

int main(int argc, char** argv)
{
    using namespace NGIN::Core;

    auto builder = CreateApplicationBuilder(argc, argv);
    builder->SetApplicationName("App.HostedCore");
    builder->Services()
        .AddDefaults()
        .AddConfiguration();
    builder->Configuration()
        .AddSource("config/app.cfg")
        .SetEnvironmentName("local")
        .SetWorkingDirectory(".");
    builder->Modules()
        .Register(StaticModuleRegistration {
            .descriptor = MakeDescriptor(),
            .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>>
            {
                return NGIN::Memory::MakeSharedAs<IModule, HostedCoreModule>();
            },
        })
        .Enable("App.HostedCore.Runtime");

    auto app = builder->Build();
    if (!app)
    {
        std::cerr << "Build failed: " << app.Error().message << "\n";
        return 1;
    }

    auto host = app.Value();
    auto start = host->Start();
    if (!start)
    {
        std::cerr << "Start failed: " << start.Error().message << "\n";
        return 2;
    }

    auto config = host->GetConfig();
    if (!config)
    {
        std::cerr << "Config store unavailable\n";
        return 3;
    }

    auto appName = config->GetRaw("App.Name");
    auto message = config->GetRaw("App.Message");
    if (!appName || appName.Value() != "App.HostedCore")
    {
        std::cerr << "App.Name was not resolved from staged config\n";
        return 4;
    }
    if (!message || message.Value() != "hello from staged hosted config")
    {
        std::cerr << "App.Message was not resolved from staged config\n";
        return 5;
    }

    auto services = host->GetServices();
    if (!services)
    {
        std::cerr << "Service registry unavailable\n";
        return 6;
    }
    auto ready = services->ResolveOptional("App.HostedCore.Ready");
    if (!ready || !ready.Value().has_value())
    {
        std::cerr << "Hosted module service was not registered\n";
        return 7;
    }

    host->RequestStop("example complete");
    auto shutdown = host->Shutdown();
    if (!shutdown)
    {
        std::cerr << "Shutdown failed: " << shutdown.Error().message << "\n";
        return 8;
    }

    std::cout << "App.HostedCore completed successfully\n";
    return 0;
}
