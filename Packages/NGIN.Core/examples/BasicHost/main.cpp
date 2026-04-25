#include <NGIN/Core/Core.hpp>
#include <NGIN/IO/Path.hpp>

#include <cstdlib>
#include <iostream>

namespace {
[[nodiscard]] auto MakeDescriptor(
    std::string name,
    const NGIN::Core::StartupStage stage = NGIN::Core::StartupStage::Features)
    -> NGIN::Core::ModuleDescriptor {
  using namespace NGIN::Core;

  ModuleDescriptor descriptor{};
  descriptor.name = std::move(name);
  descriptor.family = ModuleFamily::App;
  descriptor.type = ModuleType::Runtime;
  descriptor.version = SemanticVersion{0, 1, 0, {}};
  descriptor.compatiblePlatformRange =
      ParseVersionRange(">=0.1.0 <1.0.0").Value();
  descriptor.operatingSystems = {"linux", "windows", "macos"};
  descriptor.startupStage = stage;
  descriptor.entryKind = ModuleEntryKind::Static;
  return descriptor;
}

class CoreDemoModule final : public NGIN::Core::IModule {
public:
  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    auto logger = context.GetLogger("Demo");
    if (logger) {
      logger->Info([](NGIN::Log::RecordBuilder &rec) {
        rec.Message("CoreDemoModule started through ApplicationBuilder");
      });
    }
    return {};
  }
};

class PackageDemoModule final : public NGIN::Core::IModule {
public:
  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    auto logger = context.GetLogger("Package");
    if (logger) {
      logger->Info([](NGIN::Log::RecordBuilder &rec) {
        rec.Message("PackageDemoModule started through package bootstrap");
      });
    }
    return {};
  }
};

auto BootstrapSamplePackage(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  using namespace NGIN::Core;

  context.Services().AddSingletonValue<std::string>(
      "Samples.Package.Message", "package-ready");

  context.Modules()
      .Register(StaticModuleRegistration{
          .descriptor = MakeDescriptor("App.PackageBootstrap"),
          .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>> {
            return NGIN::Memory::MakeSharedAs<IModule, PackageDemoModule>();
          },
      })
      .Enable("App.PackageBootstrap");

  context.Plugins().AddSearchPath("plugins/sample");
  context.Configuration().AddSource("package.cfg");
  return {};
}
} // namespace

extern "C" auto
NGIN_Bootstrap_Samples_DemoPackage(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplePackage(context);
}

extern "C" void NGIN_RegisterPackage_Samples_DemoPackage(
    NGIN::Core::PackageBootstrapRegistry &registry) {
  const auto result = registry.Register({
      .packageName = "Samples.DemoPackage",
      .entryPoint = "NGIN_Bootstrap_Samples_DemoPackage",
      .fn = &NGIN_Bootstrap_Samples_DemoPackage,
  });

  if (!result) {
    std::abort();
  }
}

int main(int argc, char **argv) {
  using namespace NGIN::Core;

  const NGIN::IO::Path sampleDir{NGIN_CORE_BASIC_HOST_DIR};

  auto builder = CreateApplicationBuilder(argc, argv);
  builder->UseProjectFile(
      std::string(sampleDir.Join("NGIN.Core.BasicHost.nginproj").View()));
  builder->SetApplicationName("NGIN.Core.BasicHost");
  builder->SetConfiguration("Samples.BasicHost");
  builder->Services().AddDefaults().AddConfiguration().AddSingletonValue<std::string>(
      "App.Message", "builder-ready");
  builder->Modules()
      .Register(StaticModuleRegistration{
          .descriptor = MakeDescriptor("App.BasicHost"),
          .factory = []() -> CoreResult<NGIN::Memory::Shared<IModule>> {
            return NGIN::Memory::MakeSharedAs<IModule, CoreDemoModule>();
          },
      })
      .Enable("App.BasicHost");
  builder->Packages()
      .AddManifestFile(
          std::string(sampleDir.Join("Samples.DemoPackage.nginpkg").View()))
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_DemoPackage);

  auto app = builder->Build();
  if (!app) {
    std::cerr << "Build failed: " << app.Error().message << "\n";
    return 1;
  }

  auto host = app.Value();
  auto start = host->Start();
  if (!start) {
    std::cerr << "Start failed: " << start.Error().message << "\n";
    return 2;
  }

  auto services = host->GetServices();
  if (!services) {
    std::cerr << "services unavailable after start\n";
    return 3;
  }

  auto resolvedMessage = services->ResolveRequired<std::string>("App.Message");
  if (!resolvedMessage) {
    std::cerr << "failed to resolve App.Message: "
              << resolvedMessage.Error().message << "\n";
    return 4;
  }

  auto packageMessage = services->ResolveRequired<std::string>("Samples.Package.Message");
  if (!packageMessage) {
    std::cerr << "failed to resolve Samples.Package.Message: "
              << packageMessage.Error().message << "\n";
    return 5;
  }

  auto config = host->GetConfig();
  if (!config || !config->GetRaw("Package.Mode").HasValue()) {
    std::cerr << "package bootstrap config was not applied\n";
    return 6;
  }

  host->RequestStop("example complete");

  auto shutdown = host->Shutdown();
  if (!shutdown) {
    std::cerr << "Shutdown failed: " << shutdown.Error().message << "\n";
    return 7;
  }

  std::cout << "NGIN.Core basic host completed successfully through "
               "ApplicationBuilder\n";
  return 0;
}
