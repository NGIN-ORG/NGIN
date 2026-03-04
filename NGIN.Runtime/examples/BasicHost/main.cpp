#include <NGIN/Runtime/Runtime.hpp>

#include <iostream>

namespace
{
    class RuntimeDemoModule final : public NGIN::Runtime::IModule
    {
    public:
        auto OnStart(NGIN::Runtime::ModuleContext& context) noexcept -> NGIN::Runtime::RuntimeResult<void> override
        {
            auto logger = context.GetLogger("Demo");
            if (logger)
            {
                logger->Info([](NGIN::Log::RecordBuilder& rec) {
                    rec.Message("RuntimeDemoModule started");
                });
            }
            return {};
        }
    };
}

int main()
{
    using namespace NGIN::Runtime;

    ClearStaticModules();

    ModuleDescriptor descriptor {};
    descriptor.name = "Runtime.Demo";
    descriptor.type = ModuleType::Runtime;
    descriptor.version = SemanticVersion {0, 1, 0, {}};
    descriptor.compatiblePlatformRange = VersionRange {};
    descriptor.platforms = {"linux", "windows", "macos"};
    descriptor.loadPhase = LoadPhase::CoreServices;

    auto regResult = RegisterStaticModule(StaticModuleRegistration {
        .descriptor = descriptor,
        .factory = []() -> RuntimeResult<NGIN::Memory::Shared<IModule>> {
            auto module = NGIN::Memory::MakeSharedAs<IModule, RuntimeDemoModule>();
            if (!module)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InternalError, "Example", "Runtime.Demo", "failed to allocate module"));
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
    config.hostName = "RuntimeBasicHost";
    config.hostType = HostType::Program;
    config.platformName = "linux-x64";
    config.targetName = "NGIN.Runtime.BasicHost";
    config.enableDynamicPlugins = false;
    config.enableReflection = false;

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

    std::cout << "NGIN.Runtime basic host completed successfully\n";
    return 0;
}

