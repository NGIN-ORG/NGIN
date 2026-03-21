#include <NGIN/Core/Core.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using NGIN::Core::CoreResult;
    using NGIN::Core::DependencyDescriptor;
    using NGIN::Core::HostProfile;
    using NGIN::Core::HostType;
    using NGIN::Core::IModule;
    using NGIN::Core::ModuleContext;
    using NGIN::Core::ModuleDescriptor;
    using NGIN::Core::ModuleFamily;
    using NGIN::Core::ModuleType;
    using NGIN::Core::StartupReport;
    using NGIN::Core::StartupStage;

    struct VariantExpectations
    {
        std::string              mode {};
        std::string              environment {};
        HostProfile              profile {HostProfile::ConsoleApp};
        std::vector<std::string> expectedPackages {};
        std::vector<std::string> expectedServices {};
        std::vector<std::string> absentServices {};
    };

    class FeatureModule final : public IModule
    {
    public:
        FeatureModule(std::string serviceKey, std::string logMessage)
            : m_serviceKey(std::move(serviceKey))
            , m_logMessage(std::move(logMessage))
        {
        }

        auto OnStart(ModuleContext& context) noexcept -> CoreResult<void> override
        {
            auto logger = context.GetLogger("Startup");
            if (logger)
            {
                const auto message = m_logMessage;
                logger->Info([&](NGIN::Log::RecordBuilder& rec) {
                    rec.Message(message);
                });
            }

            return context.RegisterSingleton(m_serviceKey, NGIN::Utilities::Any<>(std::string("ready")));
        }

    private:
        std::string m_serviceKey {};
        std::string m_logMessage {};
    };

    [[nodiscard]] auto MakeDependency(std::string name) -> DependencyDescriptor
    {
        DependencyDescriptor dependency {};
        dependency.name = std::move(name);
        dependency.optional = false;
        return dependency;
    }

    [[nodiscard]] auto MakeDescriptor(
        std::string name,
        const ModuleType type = ModuleType::Runtime,
        const StartupStage stage = StartupStage::Features,
        std::vector<HostType> supportedHosts = {},
        const bool reflectionRequired = false,
        std::vector<DependencyDescriptor> dependencies = {}) -> ModuleDescriptor
    {
        ModuleDescriptor descriptor {};
        descriptor.name = std::move(name);
        descriptor.family = ModuleFamily::App;
        descriptor.type = type;
        descriptor.version = NGIN::Core::SemanticVersion {0, 1, 0, {}};
        descriptor.compatiblePlatformRange = NGIN::Core::ParseVersionRange(">=0.1.0 <1.0.0").ValueUnsafe();
        descriptor.platforms = {"linux", "windows", "macos"};
        descriptor.dependencies = std::move(dependencies);
        descriptor.startupStage = stage;
        descriptor.supportedHosts = std::move(supportedHosts);
        descriptor.entryKind = NGIN::Core::ModuleEntryKind::Static;
        descriptor.reflectionRequired = reflectionRequired;
        return descriptor;
    }

    [[nodiscard]] auto ResolveVariantOverride(const int argc, char** argv) -> std::optional<std::string>
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument = argv[index] != nullptr ? argv[index] : "";
            if (argument == "--variant" && (index + 1) < argc && argv[index + 1] != nullptr)
            {
                return std::string(argv[index + 1]);
            }

            constexpr std::string_view prefix = "--variant=";
            if (argument.rfind(prefix, 0) == 0)
            {
                return std::string(argument.substr(prefix.size()));
            }
        }

        if (const char* environmentValue = std::getenv("APP_SHOWCASE_VARIANT");
            environmentValue != nullptr && *environmentValue != '\0')
        {
            return std::string(environmentValue);
        }

        return std::nullopt;
    }

    [[nodiscard]] auto HasEntry(const std::vector<std::string>& values, const std::string_view target) -> bool
    {
        return std::find(values.begin(), values.end(), target) != values.end();
    }

    [[nodiscard]] auto ReadConfigValue(NGIN::Core::IConfigStore& config, const std::string_view key) -> std::optional<std::string>
    {
        auto value = config.GetRaw(key);
        if (!value)
        {
            return std::nullopt;
        }

        return value.ValueUnsafe();
    }

    [[nodiscard]] auto HasService(NGIN::Core::IServiceRegistry& services, const std::string_view key) -> CoreResult<bool>
    {
        auto value = services.ResolveOptional(key);
        if (!value)
        {
            return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(value.ErrorUnsafe());
        }

        return value.ValueUnsafe().has_value();
    }

    [[nodiscard]] auto GetExpectations(const std::string_view variant) -> std::optional<VariantExpectations>
    {
        if (variant == "Runtime")
        {
            return VariantExpectations {
                .mode = "runtime",
                .environment = "Dev",
                .profile = HostProfile::ConsoleApp,
                .expectedPackages = {"NGIN.Core"},
                .expectedServices = {
                    "App.Showcase.Runtime.Ready",
                    "App.Showcase.ConsoleBanner.Ready",
                },
                .absentServices = {
                    "App.Showcase.DevTools.Ready",
                    "App.Showcase.Diagnostics.Ready",
                    "App.Showcase.Reflection.Ready",
                    "App.Showcase.Service.Ready",
                },
            };
        }

        if (variant == "Runtime.DevTools")
        {
            return VariantExpectations {
                .mode = "runtime-devtools",
                .environment = "Dev",
                .profile = HostProfile::ConsoleApp,
                .expectedPackages = {"NGIN.Core"},
                .expectedServices = {
                    "App.Showcase.Runtime.Ready",
                    "App.Showcase.ConsoleBanner.Ready",
                    "App.Showcase.DevTools.Ready",
                },
                .absentServices = {
                    "App.Showcase.Diagnostics.Ready",
                    "App.Showcase.Reflection.Ready",
                    "App.Showcase.Service.Ready",
                },
            };
        }

        if (variant == "Runtime.Diagnostics")
        {
            return VariantExpectations {
                .mode = "runtime-diagnostics",
                .environment = "Diagnostics",
                .profile = HostProfile::ConsoleApp,
                .expectedPackages = {"NGIN.Core", "NGIN.Diagnostics"},
                .expectedServices = {
                    "App.Showcase.Runtime.Ready",
                    "App.Showcase.ConsoleBanner.Ready",
                    "App.Showcase.Diagnostics.Ready",
                },
                .absentServices = {
                    "App.Showcase.DevTools.Ready",
                    "App.Showcase.Reflection.Ready",
                    "App.Showcase.Service.Ready",
                },
            };
        }

        if (variant == "Runtime.Reflection")
        {
            return VariantExpectations {
                .mode = "runtime-reflection",
                .environment = "Research",
                .profile = HostProfile::ConsoleApp,
                .expectedPackages = {"NGIN.Core", "NGIN.Reflection"},
                .expectedServices = {
                    "App.Showcase.Runtime.Ready",
                    "App.Showcase.ConsoleBanner.Ready",
                    "App.Showcase.Reflection.Ready",
                },
                .absentServices = {
                    "App.Showcase.DevTools.Ready",
                    "App.Showcase.Diagnostics.Ready",
                    "App.Showcase.Service.Ready",
                },
            };
        }

        if (variant == "Service")
        {
            return VariantExpectations {
                .mode = "service",
                .environment = "Staging",
                .profile = HostProfile::Service,
                .expectedPackages = {"NGIN.Core"},
                .expectedServices = {
                    "App.Showcase.Runtime.Ready",
                    "App.Showcase.Service.Ready",
                },
                .absentServices = {
                    "App.Showcase.ConsoleBanner.Ready",
                    "App.Showcase.DevTools.Ready",
                    "App.Showcase.Diagnostics.Ready",
                    "App.Showcase.Reflection.Ready",
                },
            };
        }

        return std::nullopt;
    }

    auto ValidateConfiguration(
        NGIN::Core::IConfigStore& config,
        const VariantExpectations& expectations) -> bool
    {
        const auto appName = ReadConfigValue(config, "App.Name");
        const auto appMode = ReadConfigValue(config, "App.Mode");
        const auto appEnvironment = ReadConfigValue(config, "App.Environment");
        if (!appName || *appName != "App.Showcase")
        {
            std::cerr << "App.Name was not resolved from project config\n";
            return false;
        }
        if (!appMode || *appMode != expectations.mode)
        {
            std::cerr << "App.Mode did not match the selected variant\n";
            return false;
        }
        if (!appEnvironment || *appEnvironment != expectations.environment)
        {
            std::cerr << "App.Environment did not match the selected variant\n";
            return false;
        }

        return true;
    }

    auto ValidateStartupReport(
        const StartupReport& report,
        const VariantExpectations& expectations) -> bool
    {
        for (const auto& packageName : expectations.expectedPackages)
        {
            if (!HasEntry(report.resolvedPackages, packageName))
            {
                std::cerr << "startup report did not include expected package '" << packageName << "'\n";
                return false;
            }
        }

        return true;
    }

    auto ValidateServices(
        NGIN::Core::IServiceRegistry& services,
        const VariantExpectations& expectations) -> bool
    {
        for (const auto& serviceName : expectations.expectedServices)
        {
            auto present = HasService(services, serviceName);
            if (!present)
            {
                std::cerr << "failed to query service '" << serviceName << "': "
                          << present.ErrorUnsafe().message << "\n";
                return false;
            }
            if (!present.ValueUnsafe())
            {
                std::cerr << "expected service '" << serviceName << "' to be registered\n";
                return false;
            }
        }

        for (const auto& serviceName : expectations.absentServices)
        {
            auto present = HasService(services, serviceName);
            if (!present)
            {
                std::cerr << "failed to query service '" << serviceName << "': "
                          << present.ErrorUnsafe().message << "\n";
                return false;
            }
            if (present.ValueUnsafe())
            {
                std::cerr << "service '" << serviceName << "' should not be active for the selected variant\n";
                return false;
            }
        }

        return true;
    }

    void PrintSummary(
        const std::string& variantName,
        const StartupReport& report,
        NGIN::Core::IConfigStore& config)
    {
        const auto mode = ReadConfigValue(config, "App.Mode").value_or("<missing>");
        const auto environment = ReadConfigValue(config, "App.Environment").value_or("<missing>");
        const auto features = ReadConfigValue(config, "App.Features").value_or("<missing>");

        std::cout << "App.Showcase variant: " << variantName << "\n";
        std::cout << "  mode: " << mode << "\n";
        std::cout << "  environment: " << environment << "\n";
        std::cout << "  features: " << features << "\n";
        std::cout << "  packages:";
        for (const auto& packageName : report.resolvedPackages)
        {
            std::cout << ' ' << packageName;
        }
        std::cout << "\n";
    }

    auto RegisterFeatureModule(
        NGIN::Core::ApplicationBuilder& builder,
        ModuleDescriptor descriptor,
        std::string serviceKey,
        std::string logMessage) -> void
    {
        builder.Modules().Register(NGIN::Core::StaticModuleRegistration {
            .descriptor = std::move(descriptor),
            .factory = [serviceKey = std::move(serviceKey), logMessage = std::move(logMessage)]() -> CoreResult<NGIN::Memory::Shared<IModule>>
            {
                return NGIN::Memory::MakeSharedAs<IModule, FeatureModule>(
                    std::move(serviceKey),
                    std::move(logMessage));
            },
        });
    }
}

int main(int argc, char** argv)
{
    using namespace NGIN::Core;

    const auto exampleRoot = std::filesystem::path(APP_SHOWCASE_EXAMPLE_ROOT);
    const auto projectPath = (exampleRoot / "App.Showcase.nginproj").lexically_normal();
    const auto selectedVariant = ResolveVariantOverride(argc, argv);

    auto builder = CreateApplicationBuilder(argc, argv);
    builder->UseProjectFile(projectPath.string());
    builder->SetApplicationName("App.Showcase");
    if (selectedVariant.has_value())
    {
        builder->SetDefaultVariant(*selectedVariant);
    }

    builder->Services()
        .AddDefaults()
        .AddConfiguration();

    RegisterFeatureModule(
        *builder,
        MakeDescriptor("App.Showcase.Runtime", ModuleType::Runtime, StartupStage::Services),
        "App.Showcase.Runtime.Ready",
        "App.Showcase.Runtime started");
    RegisterFeatureModule(
        *builder,
        MakeDescriptor(
            "App.Showcase.ConsoleBanner",
            ModuleType::Runtime,
            StartupStage::Presentation,
            {HostType::ConsoleApp},
            false,
            {MakeDependency("App.Showcase.Runtime")}),
        "App.Showcase.ConsoleBanner.Ready",
        "App.Showcase.ConsoleBanner started");
    RegisterFeatureModule(
        *builder,
        MakeDescriptor(
            "App.Showcase.DevTools",
            ModuleType::Developer,
            StartupStage::Presentation,
            {HostType::ConsoleApp},
            false,
            {MakeDependency("App.Showcase.Runtime")}),
        "App.Showcase.DevTools.Ready",
        "App.Showcase.DevTools started");
    RegisterFeatureModule(
        *builder,
        MakeDescriptor(
            "App.Showcase.Diagnostics",
            ModuleType::Developer,
            StartupStage::Services,
            {HostType::ConsoleApp},
            false,
            {MakeDependency("App.Showcase.Runtime")}),
        "App.Showcase.Diagnostics.Ready",
        "App.Showcase.Diagnostics started");
    RegisterFeatureModule(
        *builder,
        MakeDescriptor(
            "App.Showcase.Reflection",
            ModuleType::Developer,
            StartupStage::Services,
            {HostType::ConsoleApp},
            true,
            {MakeDependency("App.Showcase.Runtime")}),
        "App.Showcase.Reflection.Ready",
        "App.Showcase.Reflection started");
    RegisterFeatureModule(
        *builder,
        MakeDescriptor(
            "App.Showcase.Service",
            ModuleType::Runtime,
            StartupStage::Services,
            {HostType::Service},
            false,
            {MakeDependency("App.Showcase.Runtime")}),
        "App.Showcase.Service.Ready",
        "App.Showcase.Service started");

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

    const auto variantName = host->GetVariantName();
    const auto expectations = GetExpectations(variantName);
    if (!expectations.has_value())
    {
        std::cerr << "No expectations defined for variant '" << variantName << "'\n";
        return 3;
    }

    if (host->GetProfile() != expectations->profile)
    {
        std::cerr << "Host profile did not match the selected variant\n";
        return 4;
    }

    auto config = host->GetConfig();
    if (!config)
    {
        std::cerr << "Config store unavailable\n";
        return 5;
    }

    if (!ValidateConfiguration(*config, *expectations))
    {
        return 6;
    }

    const auto report = host->GetStartupReport();
    if (!ValidateStartupReport(report, *expectations))
    {
        return 7;
    }

    auto services = host->GetServices();
    if (!services)
    {
        std::cerr << "Service registry unavailable\n";
        return 8;
    }

    if (!ValidateServices(*services, *expectations))
    {
        return 9;
    }

    PrintSummary(variantName, report, *config);

    host->RequestStop("example complete");

    auto shutdown = host->Shutdown();
    if (!shutdown)
    {
        std::cerr << "Shutdown failed: " << shutdown.ErrorUnsafe().message << "\n";
        return 10;
    }

    std::cout << "App.Showcase completed successfully\n";
    return 0;
}
