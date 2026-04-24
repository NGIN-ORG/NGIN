#include <NGIN/Core/Application.hpp>

namespace
{
    using namespace NGIN::Core;

    auto BootstrapEcs(PackageBootstrapContext &context) -> CoreResult<void>
    {
        context.Services()
            .AddSingleton("NGIN.ECS.Registry", NGIN::Utilities::Any<>(std::string("ecs-registry")))
            .AddScoped(
                "NGIN.ECS.WorldFactory",
                []() -> CoreResult<NGIN::Utilities::Any<>>
                {
                    return NGIN::Utilities::Any<>(std::string("ecs-world"));
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
