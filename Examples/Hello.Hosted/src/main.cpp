#include <NGIN/Core/Core.hpp>

#include <iostream>

namespace
{
    class HostedCoreModule final : public NGIN::Core::IModule
    {
    public:
        auto OnStart(NGIN::Core::ModuleContext& context) noexcept -> NGIN::Core::CoreResult<void> override
        {
            auto logger = context.GetLogger("Startup");
            if (logger)
            {
                logger->Info([](NGIN::Log::RecordBuilder& rec) {
                    rec.Message("Hello.Hosted startup module started");
                });
            }
            return context.RegisterSingletonValue<bool>("Hello.Hosted.Ready", true);
        }
    };
}

int main(int argc, char** argv)
{
    using namespace NGIN::Core;

    auto builder = CreateApplicationBuilder(argc, argv);
    builder->SetApplicationName("Hello.Hosted")
        .AddDefaultServices()
        .AddConfiguration()
        .AddConfigSource("config/app.cfg")
        .AddModule<HostedCoreModule>("Hello.Hosted.Startup");

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
    if (!appName || appName.Value() != "Hello.Hosted")
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
    auto ready = services->ResolveOptional<bool>("Hello.Hosted.Ready");
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

    std::cout << "Hello.Hosted completed successfully\n";
    return 0;
}
