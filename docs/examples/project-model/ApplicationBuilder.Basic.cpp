#include <NGIN/Core/Application.hpp>

extern "C" void NGIN_RegisterPackage_NGIN_ECS(NGIN::Core::PackageBootstrapRegistry& registry);

int main(int argc, char** argv)
{
    auto builder = NGIN::Core::CreateApplicationBuilder(argc, argv);

    builder->SetApplicationName("Sandbox.Game");

    builder->Services()
        .AddDefaults()
        .AddLogging()
        .AddConfiguration()
        .AddSingletonValue<std::string>("Game.Name", "Sandbox.Game");

    builder->Packages()
        .RegisterLinkedRegistrar(&NGIN_RegisterPackage_NGIN_ECS)
        .ApplyBootstrap("NGIN.ECS");

    builder->Configuration()
        .SetEnvironmentName("Dev")
        .AddSource("config/game.xml")
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
