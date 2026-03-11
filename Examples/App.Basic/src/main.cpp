#include <NGIN/Core/Core.hpp>

#include <filesystem>
#include <iostream>

namespace
{
    [[nodiscard]] auto MakeDescriptor() -> NGIN::Core::ModuleDescriptor
    {
        using namespace NGIN::Core;

        ModuleDescriptor descriptor {};
        descriptor.name = "App.Basic.Runtime";
        descriptor.family = ModuleFamily::App;
        descriptor.type = ModuleType::Runtime;
        descriptor.version = SemanticVersion {0, 1, 0, {}};
        descriptor.compatiblePlatformRange = ParseVersionRange(">=0.1.0 <1.0.0").ValueUnsafe();
        descriptor.platforms = {"linux", "windows", "macos"};
        descriptor.loadPhase = LoadPhase::Application;
        descriptor.entryKind = ModuleEntryKind::Static;
        return descriptor;
    }

    class AppBasicModule final : public NGIN::Core::IModule
    {
    public:
        auto OnStart(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            auto logger = context.GetLogger("Startup");
            if (logger)
            {
                logger->Info([](NGIN::Log::RecordBuilder& rec) {
                    rec.Message("App.Basic project module started");
                });
            }
            return {};
        }
    };
}

int main(int argc, char** argv)
{
    using namespace NGIN::Core;

    const auto exampleRoot = std::filesystem::path(APP_BASIC_EXAMPLE_ROOT);
    const auto projectPath = (exampleRoot / "App.Basic.nginproj").lexically_normal();

    auto builder = CreateApplicationBuilder(argc, argv);
    builder->UseProjectFile(projectPath.string());
    builder->SetApplicationName("App.Basic");
    builder->UseProfile(HostProfile::ConsoleApp);
    builder->Services()
        .AddDefaults()
        .AddConfiguration();
    builder->Modules()
        .Register(StaticModuleRegistration {
            .descriptor = MakeDescriptor(),
            .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>>
            {
                return NGIN::Memory::MakeSharedAs<IModule, AppBasicModule>();
            },
        })
        .Enable("App.Basic.Runtime");

    auto app = builder->Build();
    if (!app)
    {
        std::cerr << "Build failed: " << app.ErrorUnsafe().message << "\n";
        return 1;
    }

    auto host = app.ValueUnsafe();
    auto start = host->Start();
    if (!start)
    {
        std::cerr << "Start failed: " << start.ErrorUnsafe().message << "\n";
        return 2;
    }

    auto config = host->GetConfig();
    if (!config)
    {
        std::cerr << "Config store unavailable\n";
        return 3;
    }

    auto appName = config->GetRaw("App.Name");
    auto appMessage = config->GetRaw("App.Message");
    if (!appName || appName.ValueUnsafe() != "App.Basic")
    {
        std::cerr << "App.Name was not resolved from project config\n";
        return 4;
    }
    if (!appMessage || appMessage.ValueUnsafe() != "hello from App.Basic")
    {
        std::cerr << "App.Message was not resolved from project config\n";
        return 5;
    }

    host->RequestStop("example complete");

    auto shutdown = host->Shutdown();
    if (!shutdown)
    {
        std::cerr << "Shutdown failed: " << shutdown.ErrorUnsafe().message << "\n";
        return 6;
    }

    std::cout << "App.Basic completed successfully\n";
    return 0;
}
