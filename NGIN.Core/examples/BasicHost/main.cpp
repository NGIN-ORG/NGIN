#include <NGIN/Core/Core.hpp>

#include <filesystem>
#include <iostream>

namespace
{
    class CoreDemoModule final : public NGIN::Core::IModule
    {
    public:
        auto OnStart(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            auto logger = context.GetLogger("Demo");
            if (logger)
            {
                logger->Info([](NGIN::Log::RecordBuilder& rec) {
                    rec.Message("CoreDemoModule started through ApplicationBuilder");
                });
            }
            return {};
        }
    };

    struct GlobalStaticModuleReset
    {
        ~GlobalStaticModuleReset()
        {
            NGIN::Core::ClearStaticModules();
        }
    };
}

int main(int argc, char** argv)
{
    using namespace NGIN::Core;

    ClearStaticModules();
    GlobalStaticModuleReset reset;

    ModuleDescriptor descriptor {};
    descriptor.name = "App.BasicHost";
    descriptor.family = ModuleFamily::App;
    descriptor.type = ModuleType::Runtime;
    descriptor.version = SemanticVersion {0, 1, 0, {}};
    descriptor.compatiblePlatformRange = ParseVersionRange(">=0.1.0 <1.0.0").ValueUnsafe();
    descriptor.platforms = {"linux", "windows", "macos"};
    descriptor.loadPhase = LoadPhase::Application;

    auto regResult = RegisterStaticModule(StaticModuleRegistration {
        .descriptor = descriptor,
        .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>> {
            auto module = NGIN::Memory::MakeSharedAs<IModule, CoreDemoModule>();
            if (!module)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InternalError, "Example", "App.BasicHost", "failed to allocate module"));
            }
            return module;
        },
    });

    if (!regResult)
    {
        std::cerr << "registration failed: " << regResult.ErrorUnsafe().message << "\n";
        return 1;
    }

    const auto projectFile = std::filesystem::path(NGIN_CORE_BASIC_HOST_DIR) / "ngin.project.json";

    auto builder = CreateApplicationBuilder(argc, argv);
    builder->UseProjectFile(projectFile.string());
    builder->SetApplicationName("NGIN.Core.BasicHost");
    builder->SetDefaultTarget("Samples.BasicHost");
    builder->UseProfile(HostProfile::ConsoleApp);
    builder->Services()
        .AddDefaults()
        .AddConfiguration()
        .AddSingleton("App.Message", NGIN::Utilities::Any<>(std::string("builder-ready")));
    builder->Modules().Enable("App.BasicHost");

    auto app = builder->Build();
    if (!app)
    {
        std::cerr << "Build failed: " << app.ErrorUnsafe().message << "\n";
        return 2;
    }

    auto host = app.ValueUnsafe();

    auto start = host->Start();
    if (!start)
    {
        std::cerr << "Start failed: " << start.ErrorUnsafe().message << "\n";
        return 3;
    }

    auto services = host->GetServices();
    if (!services)
    {
        std::cerr << "services unavailable after start\n";
        return 4;
    }
    auto resolvedMessage = services->ResolveRequired("App.Message");
    if (!resolvedMessage)
    {
        std::cerr << "failed to resolve App.Message: " << resolvedMessage.ErrorUnsafe().message << "\n";
        return 5;
    }

    host->RequestStop("example complete");

    auto shutdown = host->Shutdown();
    if (!shutdown)
    {
        std::cerr << "Shutdown failed: " << shutdown.ErrorUnsafe().message << "\n";
        return 6;
    }

    std::cout << "NGIN.Core basic host completed successfully through ApplicationBuilder\n";
    return 0;
}
