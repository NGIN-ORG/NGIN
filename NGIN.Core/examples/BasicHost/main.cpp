#include <NGIN/Core/Core.hpp>

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
                    rec.Message("CoreDemoModule started");
                });
            }
            return {};
        }
    };
}

int main()
{
    using namespace NGIN::Core;

    auto moduleCatalog = CreateStaticModuleCatalog();
    if (!moduleCatalog)
    {
        std::cerr << "failed to create module catalog\n";
        return 1;
    }

    ModuleDescriptor descriptor {};
    descriptor.name = "Core.Demo";
    descriptor.family = ModuleFamily::Core;
    descriptor.type = ModuleType::Runtime;
    descriptor.version = SemanticVersion {0, 1, 0, {}};
    descriptor.compatiblePlatformRange = ParseVersionRange(">=0.1.0 <1.0.0").ValueUnsafe();
    descriptor.platforms = {"linux", "windows", "macos"};
    descriptor.loadPhase = LoadPhase::CoreServices;

    auto regResult = moduleCatalog->Register(StaticModuleRegistration {
        .descriptor = descriptor,
        .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>> {
            auto module = NGIN::Memory::MakeSharedAs<IModule, CoreDemoModule>();
            if (!module)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InternalError, "Example", "Core.Demo", "failed to allocate module"));
            }
            return module;
        },
    });

    if (!regResult)
    {
        std::cerr << "registration failed: " << regResult.ErrorUnsafe().message << "\n";
        return 1;
    }

    KernelHostConfig config {};
    config.hostName = "CoreBasicHost";
    config.hostType = HostType::ConsoleApp;
    config.platformName = "linux-x64";
    config.platformVersion = SemanticVersion {0, 1, 0, {}};
    config.targetName = "NGIN.Core.BasicHost";
    config.enableDynamicPlugins = false;
    config.enableReflection = false;
    config.moduleCatalog = moduleCatalog;

    auto kernelResult = CreateKernel(config);
    if (!kernelResult)
    {
        std::cerr << "CreateKernel failed: " << kernelResult.ErrorUnsafe().message << "\n";
        return 2;
    }

    auto kernel = kernelResult.ValueUnsafe();

    auto start = kernel->Start();
    if (!start)
    {
        std::cerr << "Start failed: " << start.ErrorUnsafe().message << "\n";
        return 3;
    }

    kernel->RequestStop("example complete");

    auto shutdown = kernel->Shutdown();
    if (!shutdown)
    {
        std::cerr << "Shutdown failed: " << shutdown.ErrorUnsafe().message << "\n";
        return 4;
    }

    std::cout << "NGIN.Core basic host completed successfully\n";
    return 0;
}
