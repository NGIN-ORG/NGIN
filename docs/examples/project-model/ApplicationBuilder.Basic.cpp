#include <NGIN/Core/Application.hpp>

extern "C" void NGIN_RegisterPackage_NGIN_ECS(NGIN::Core::PackageBootstrapRegistry& registry);

int main(int argc, char** argv)
{
    auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);

    builder->UseProjectFile("ngin.project.json");
    builder->SetApplicationName("Sandbox.Game");
    builder->SetDefaultTarget("Sandbox.Game");
    builder->UseProfile(NGIN::Core::HostProfile::Game);

    builder->Services()
        .AddDefaults()
        .AddLogging()
        .AddConfiguration()
        .AddSingleton("Game.Name", NGIN::Utilities::Any<>(std::string("Sandbox.Game")));

    builder->Packages().Add({
        .name = "NGIN.ECS",
        .versionRange = ">=0.1.0 <1.0.0",
        .optional = false,
    });
    builder->Packages()
        .AddManifestFile("ngin.package.json")
        .RegisterLinkedRegistrar(&NGIN_RegisterPackage_NGIN_ECS)
        .ApplyBootstrap("NGIN.ECS");

    builder->Modules()
        .Enable("Core.Hosting")
        .Enable("Domain.ECS");

    builder->Configuration()
        .SetEnvironmentName("Dev")
        .AddSource("config/game.json")
        .SetWorkingDirectory(".");

    auto app = builder->Build();
    if (!app)
    {
        return 1;
    }

    auto start = app.value()->Start();
    if (!start)
    {
        return 2;
    }

    auto run = app.value()->Run();
    if (!run)
    {
        return 3;
    }

    auto shutdown = app.value()->Shutdown();
    return shutdown ? 0 : 4;
}
