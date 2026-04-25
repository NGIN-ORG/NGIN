#include <NGIN/Core/Application.hpp>

namespace
{
    using namespace NGIN::Core;

    auto BootstrapEcs(PackageBootstrapContext &context) -> CoreResult<void>
    {
        context.Services()
            .AddSingletonValue<std::string>("NGIN.ECS.Registry", "ecs-registry")
            .AddScoped<std::string>(
                "NGIN.ECS.WorldFactory",
                [](ServiceResolutionContext &) -> CoreResult<NGIN::Memory::Shared<std::string>>
                {
                    return NGIN::Memory::MakeShared<std::string>("ecs-world");
                });
        return {};
    }
}

extern "C" auto NGIN_Bootstrap_NGIN_ECS(
    NGIN::Core::PackageBootstrapContext &context) -> NGIN::Core::CoreResult<void>
{
    return BootstrapEcs(context);
}

extern "C" void NGIN_RegisterPackage_NGIN_ECS(
    NGIN::Core::PackageBootstrapRegistry &registry)
{
    (void)registry.Register({
        .packageName = "NGIN.ECS",
        .entryPoint = "NGIN_Bootstrap_NGIN_ECS",
        .fn = &NGIN_Bootstrap_NGIN_ECS,
    });
}
