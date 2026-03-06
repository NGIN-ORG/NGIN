#include <NGIN/Core/Application.hpp>

namespace
{
    using namespace NGIN::Core;

    class EcsModule final : public IModule
    {
    };

    [[nodiscard]] auto MakeDescriptor() -> ModuleDescriptor
    {
        ModuleDescriptor descriptor {};
        descriptor.name = "Domain.ECS";
        descriptor.family = ModuleFamily::Domain;
        descriptor.type = ModuleType::Runtime;
        descriptor.version = SemanticVersion {0, 1, 0, {}};
        descriptor.compatiblePlatformRange = ParseVersionRange(">=0.1.0 <1.0.0").ValueUnsafe();
        descriptor.platforms = {"linux", "windows", "macos"};
        descriptor.loadPhase = LoadPhase::Domain;
        descriptor.entryKind = ModuleEntryKind::Static;
        return descriptor;
    }

    auto BootstrapEcs(PackageBootstrapContext& context) -> CoreResult<void>
    {
        context.Services()
            .AddSingleton("NGIN.ECS.Registry", NGIN::Utilities::Any<>(std::string("ecs-registry")))
            .AddScoped(
                "NGIN.ECS.WorldFactory",
                []() -> CoreResult<NGIN::Utilities::Any<>>
                {
                    return NGIN::Utilities::Any<>(std::string("ecs-world"));
                });

        context.Modules()
            .Register({
                .descriptor = MakeDescriptor(),
                .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>>
                {
                    return NGIN::Memory::MakeSharedAs<IModule, EcsModule>();
                },
            })
            .Enable("Domain.ECS");
        return {};
    }
}

extern "C" auto NGIN_Bootstrap_NGIN_ECS(
    NGIN::Core::PackageBootstrapContext& context) -> NGIN::Core::CoreResult<void>
{
    return BootstrapEcs(context);
}

extern "C" void NGIN_RegisterPackage_NGIN_ECS(
    NGIN::Core::PackageBootstrapRegistry& registry)
{
    (void)registry.Register({
        .packageName = "NGIN.ECS",
        .entryPoint = "NGIN_Bootstrap_NGIN_ECS",
        .fn = &NGIN_Bootstrap_NGIN_ECS,
    });
}
