#include <catch2/catch_test_macros.hpp>

#include <NGIN/Core/Core.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/VirtualFileSystem.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
[[nodiscard]] auto ToString(const NGIN::IO::Path &path) -> std::string {
  return std::string(path.View());
}

[[nodiscard]] auto MakeTempDir(const std::string_view prefix)
    -> NGIN::IO::Path {
  NGIN::IO::LocalFileSystem fs;
  auto tempDir = fs.CreateTempDirectory({}, prefix);
  REQUIRE(tempDir.HasValue());
  return tempDir.Value();
}

void RemovePath(const NGIN::IO::Path &path) {
  NGIN::IO::LocalFileSystem fs;
  NGIN::IO::RemoveOptions options{};
  options.recursive = true;
  options.ignoreMissing = true;
  REQUIRE(fs.RemoveAll(path, options).HasValue());
}

struct AutoConstructedService {
  inline static NGIN::UInt32 constructed{0};

  AutoConstructedService() { ++constructed; }
};

struct ProviderDependencyService {
  inline static NGIN::UInt32 constructed{0};
  NGIN::UInt32 value{7};

  ProviderDependencyService() { ++constructed; }
};

struct ProviderConsumerService {
  inline static NGIN::UInt32 constructed{0};

  explicit ProviderConsumerService(
      NGIN::Memory::Shared<NGIN::Core::IServiceProvider> provider)
      : services(std::move(provider)) {
    ++constructed;
    auto resolved = services->ResolveRequired<ProviderDependencyService>();
    if (resolved.HasValue()) {
      dependency = resolved.Value();
    }
  }

  NGIN::Memory::Shared<NGIN::Core::IServiceProvider> services{};
  NGIN::Memory::Shared<ProviderDependencyService> dependency{};
};

[[nodiscard]] auto
MakeMountedVirtualFileSystem(const NGIN::IO::Path &realRoot,
                             const NGIN::IO::Path &virtualPrefix)
    -> NGIN::Memory::Shared<NGIN::IO::IFileSystem> {
  auto fileSystem = NGIN::Memory::MakeSharedAs<NGIN::IO::IFileSystem,
                                               NGIN::IO::VirtualFileSystem>();
  auto *virtualFileSystem =
      dynamic_cast<NGIN::IO::VirtualFileSystem *>(fileSystem.Get());
  REQUIRE(virtualFileSystem != nullptr);

  virtualFileSystem->AddMount(std::make_shared<NGIN::IO::LocalMount>(
      realRoot, NGIN::IO::MountPoint{.virtualPrefix = virtualPrefix}));

  return fileSystem;
}

[[nodiscard]] auto Range(const std::string_view text)
    -> NGIN::Core::VersionRange {
  auto parsed = NGIN::Core::ParseVersionRange(text);
  REQUIRE(parsed.HasValue());
  return parsed.Value();
}

[[nodiscard]] auto Version(const NGIN::UInt32 major, const NGIN::UInt32 minor,
                           const NGIN::UInt32 patch)
    -> NGIN::Core::SemanticVersion {
  return NGIN::Core::SemanticVersion{major, minor, patch, {}};
}

[[nodiscard]] auto MakeDescriptor(
    std::string name,
    const NGIN::Core::ModuleFamily family = NGIN::Core::ModuleFamily::Core,
    const NGIN::Core::StartupStage stage = NGIN::Core::StartupStage::Features,
    const NGIN::Core::ModuleType type = NGIN::Core::ModuleType::Runtime)
    -> NGIN::Core::ModuleDescriptor {
  NGIN::Core::ModuleDescriptor desc{};
  desc.name = std::move(name);
  desc.family = family;
  desc.type = type;
  desc.version = Version(0, 1, 0);
  desc.compatiblePlatformRange = Range(">=0.1.0 <1.0.0");
  desc.operatingSystems = {"linux", "windows", "macos"};
  desc.startupStage = stage;
  desc.entryKind = NGIN::Core::ModuleEntryKind::Static;
  return desc;
}

[[nodiscard]] auto
MakeHostConfig(const NGIN::Memory::Shared<NGIN::Core::IModuleCatalog> &catalog)
    -> NGIN::Core::KernelHostConfig {
  NGIN::Core::KernelHostConfig cfg{};
  cfg.hostName = "Core.Tests";
  cfg.hostType = NGIN::Core::HostType::ConsoleApp;
  cfg.platformName = "linux-x64";
  cfg.platformVersion = Version(0, 1, 0);
  cfg.targetName = "Core.Tests.Target";
  cfg.enableDynamicPlugins = false;
  cfg.enableReflection = false;
  cfg.logSinkConfig.includeConsoleSink = false;
  cfg.moduleCatalog = catalog;
  cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::Serialized;
  return cfg;
}

auto RegisterModule(
    const NGIN::Memory::Shared<NGIN::Core::IModuleCatalog> &catalog,
    NGIN::Core::ModuleDescriptor descriptor, NGIN::Core::ModuleFactory factory)
    -> NGIN::Core::CoreResult<void> {
  return catalog->Register(NGIN::Core::StaticModuleRegistration{
      .descriptor = std::move(descriptor),
      .factory = std::move(factory),
  });
}

[[nodiscard]] auto MakeRegistration(NGIN::Core::ModuleDescriptor descriptor,
                                    NGIN::Core::ModuleFactory factory)
    -> NGIN::Core::StaticModuleRegistration {
  return NGIN::Core::StaticModuleRegistration{
      .descriptor = std::move(descriptor),
      .factory = std::move(factory),
  };
}

[[nodiscard]] auto ContainsString(const std::vector<std::string> &values,
                                  const std::string_view expected) -> bool {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] auto
ContainsWarningMessage(const std::vector<NGIN::Core::StartupWarning> &values,
                       const std::string_view expected) -> bool {
  return std::any_of(values.begin(), values.end(),
                     [&](const NGIN::Core::StartupWarning &warning) {
                       return warning.message == expected;
                     });
}

void WriteTextFile(const NGIN::IO::Path &path, const std::string &text) {
  NGIN::IO::LocalFileSystem fs;
  const auto parent = path.Parent();
  if (!parent.IsEmpty()) {
    NGIN::IO::DirectoryCreateOptions options{};
    options.recursive = true;
    REQUIRE(fs.CreateDirectories(parent, options).HasValue());
  }
  REQUIRE(NGIN::IO::WriteAllText(fs, path, text).HasValue());
}

class HookModule final : public NGIN::Core::IModule {
public:
  enum class FailStage { None, Register, Init, Start };

  HookModule(std::string name, std::vector<std::string> *order,
             std::mutex *lock, const FailStage failStage = FailStage::None)
      : m_name(std::move(name)), m_order(order), m_lock(lock),
        m_failStage(failStage) {}

  auto OnRegister(NGIN::Core::ModuleContext &) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Push("register:" + m_name);
    if (m_failStage == FailStage::Register) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          NGIN::Core::MakeKernelError(
              NGIN::Core::KernelErrorCode::ModuleLifecycleFailure, "Test",
              m_name, "forced register failure"));
    }
    return {};
  }

  auto OnInit(NGIN::Core::ModuleContext &) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Push("init:" + m_name);
    if (m_failStage == FailStage::Init) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          NGIN::Core::MakeKernelError(
              NGIN::Core::KernelErrorCode::ModuleLifecycleFailure, "Test",
              m_name, "forced init failure"));
    }
    return {};
  }

  auto OnStart(NGIN::Core::ModuleContext &) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Push("start:" + m_name);
    if (m_failStage == FailStage::Start) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          NGIN::Core::MakeKernelError(
              NGIN::Core::KernelErrorCode::ModuleLifecycleFailure, "Test",
              m_name, "forced start failure"));
    }
    return {};
  }

  auto OnStop(NGIN::Core::ModuleContext &) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Push("stop:" + m_name);
    return {};
  }

  auto OnShutdown(NGIN::Core::ModuleContext &) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Push("shutdown:" + m_name);
    return {};
  }

private:
  void Push(const std::string &value) {
    if (m_order == nullptr || m_lock == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> guard(*m_lock);
    m_order->push_back(value);
  }

  std::string m_name;
  std::vector<std::string> *m_order{nullptr};
  std::mutex *m_lock{nullptr};
  FailStage m_failStage{FailStage::None};
};

class ScopedServiceModule final : public NGIN::Core::IModule {
public:
  explicit ScopedServiceModule(std::string key) : m_key(std::move(key)) {}

  auto OnRegister(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    return context.RegisterFactory<NGIN::UInt32>(
        m_key,
        [](NGIN::Core::ServiceResolutionContext &)
            -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::UInt32>> {
          return NGIN::Memory::MakeShared<NGIN::UInt32>(42);
        },
        NGIN::Core::ServiceLifetime::Scoped);
  }

private:
  std::string m_key;
};

class ServiceProviderModule final : public NGIN::Core::IModule {
public:
  explicit ServiceProviderModule(std::string key) : m_key(std::move(key)) {}

  auto OnRegister(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    return context.RegisterSingletonValue<std::string>(m_key, "provided");
  }

private:
  std::string m_key;
};

struct TestUserEvent {
  NGIN::UInt32 value{0};
};

struct TestScopedEvent {
  std::string name{};
};

class EventProbeModule final : public NGIN::Core::IModule {
public:
  EventProbeModule(std::vector<std::string> *events, std::mutex *lock)
      : m_events(events), m_lock(lock) {}

  auto OnRegister(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    auto subscribe = [&]<typename TEvent>() -> NGIN::Core::CoreResult<void> {
      auto sub = context.Events().Subscribe<TEvent>(
          [this](const NGIN::Core::TypedEventRecord<TEvent> &eventRecord) {
            if (m_events == nullptr || m_lock == nullptr) {
              return;
            }
            std::lock_guard<std::mutex> guard(*m_lock);
            m_events->push_back(eventRecord.metadata.channel);
          },
          NGIN::Core::EventScope{.owner = std::string(context.ModuleName())});
      if (!sub) {
        return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
            sub.Error());
      }
      m_tokens.push_back(sub.Value());
      return {};
    };

    auto result = subscribe.operator()<NGIN::Core::ModuleLoadedEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.Error());
    }

    result = subscribe.operator()<NGIN::Core::ModuleStartedEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.Error());
    }

    result = subscribe.operator()<NGIN::Core::KernelRunningEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.Error());
    }

    result = subscribe.operator()<NGIN::Core::KernelStoppingEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.Error());
    }

    return {};
  }

private:
  std::vector<std::string> *m_events{nullptr};
  std::mutex *m_lock{nullptr};
  std::vector<NGIN::Core::EventSubscriptionToken> m_tokens{};
};

class RawEventProbeModule final : public NGIN::Core::IModule {
public:
  RawEventProbeModule(std::vector<std::string> *events, std::mutex *lock)
      : m_events(events), m_lock(lock) {}

  auto OnRegister(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    for (const auto channel :
         {"ModuleLoaded", "ModuleStarted", "KernelRunning", "KernelStopping"}) {
      auto sub = context.Events().SubscribeRaw(
          channel,
          [this](const NGIN::Core::RawEventRecord &eventRecord) {
            if (m_events == nullptr || m_lock == nullptr) {
              return;
            }
            std::lock_guard<std::mutex> guard(*m_lock);
            m_events->push_back(eventRecord.channel);
          },
          NGIN::Core::EventScope{.owner = std::string(context.ModuleName())});
      if (!sub) {
        return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
            sub.Error());
      }
      m_tokens.push_back(sub.Value());
    }

    return {};
  }

private:
  std::vector<std::string> *m_events{nullptr};
  std::mutex *m_lock{nullptr};
  std::vector<NGIN::Core::EventSubscriptionToken> m_tokens{};
};

std::vector<std::string> *g_packageBootstrapOrder = nullptr;

auto BootstrapSamplesPackage(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  using namespace NGIN::Core;

  context.Services().AddSingletonValue<std::string>(
      "Samples.Package.Message", "bootstrapped");

  context.Modules()
      .Register(MakeRegistration(
          MakeDescriptor("App.PackageBootstrap", ModuleFamily::App,
                         StartupStage::Features),
          []() -> CoreResult<NGIN::Memory::Shared<IModule>> {
            return NGIN::Memory::MakeSharedAs<IModule, HookModule>(
                "PackageBootstrap", nullptr, nullptr);
          }))
      .Enable("App.PackageBootstrap");

  context.Plugins().AddSearchPath("plugins/package");
  context.Configuration().AddSource("package.cfg");
  return {};
}

auto BootstrapSamplesPackageAlt(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  context.Services().AddSingletonValue<std::string>(
      "Samples.Package.Message", "bootstrapped-alt");
  return {};
}

auto BootstrapSamplesPackageA(NGIN::Core::PackageBootstrapContext &)
    -> NGIN::Core::CoreResult<void> {
  if (g_packageBootstrapOrder != nullptr) {
    g_packageBootstrapOrder->push_back("Samples.PackageA");
  }
  return {};
}

auto BootstrapSamplesPackageB(NGIN::Core::PackageBootstrapContext &)
    -> NGIN::Core::CoreResult<void> {
  if (g_packageBootstrapOrder != nullptr) {
    g_packageBootstrapOrder->push_back("Samples.PackageB");
  }
  return {};
}
} // namespace

extern "C" auto
NGIN_Bootstrap_Samples_Package(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackage(context);
}

extern "C" auto
NGIN_Bootstrap_Samples_PackageAlt(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackageAlt(context);
}

extern "C" auto
NGIN_Bootstrap_Samples_PackageA(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackageA(context);
}

extern "C" auto
NGIN_Bootstrap_Samples_PackageB(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackageB(context);
}

extern "C" void NGIN_RegisterPackage_Samples_Package(
    NGIN::Core::PackageBootstrapRegistry &registry) {
  auto first = registry.Register({
      .packageName = "Samples.Package",
      .entryPoint = "NGIN_Bootstrap_Samples_Package",
      .fn = &NGIN_Bootstrap_Samples_Package,
  });
  if (!first) {
    std::abort();
  }

  auto second = registry.Register({
      .packageName = "Samples.Package",
      .entryPoint = "NGIN_Bootstrap_Samples_PackageAlt",
      .fn = &NGIN_Bootstrap_Samples_PackageAlt,
  });
  if (!second) {
    std::abort();
  }
}

extern "C" void NGIN_RegisterPackage_Samples_PackageA(
    NGIN::Core::PackageBootstrapRegistry &registry) {
  const auto result = registry.Register({
      .packageName = "Samples.PackageA",
      .entryPoint = "NGIN_Bootstrap_Samples_PackageA",
      .fn = &NGIN_Bootstrap_Samples_PackageA,
  });
  if (!result) {
    std::abort();
  }
}

extern "C" void NGIN_RegisterPackage_Samples_PackageB(
    NGIN::Core::PackageBootstrapRegistry &registry) {
  const auto result = registry.Register({
      .packageName = "Samples.PackageB",
      .entryPoint = "NGIN_Bootstrap_Samples_PackageB",
      .fn = &NGIN_Bootstrap_Samples_PackageB,
  });
  if (!result) {
    std::abort();
  }
}

extern "C" void NGIN_RegisterPackage_Samples_PackageSingleNoAbort(
    NGIN::Core::PackageBootstrapRegistry &registry) {
  (void)registry.Register({
      .packageName = "Samples.Package",
      .entryPoint = "NGIN_Bootstrap_Samples_Package",
      .fn = &NGIN_Bootstrap_Samples_Package,
  });
}

TEST_CASE("KernelStartsWithDependencyOrderedModules", "[runtime][startup]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  std::vector<std::string> order;
  std::mutex orderLock;

  auto a = MakeDescriptor("Core.A", NGIN::Core::ModuleFamily::Core,
                          NGIN::Core::StartupStage::Foundation);
  auto b = MakeDescriptor("Core.B");
  b.dependencies.push_back(NGIN::Core::DependencyDescriptor{
      .name = "Core.A",
      .optional = false,
      .requiredVersion = Range(">=0.1.0 <1.0.0"),
  });

  REQUIRE(
      RegisterModule(
          catalog, a,
          [&]() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "A", &order, &orderLock);
          })
          .HasValue());
  REQUIRE(
      RegisterModule(
          catalog, b,
          [&]() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "B", &order, &orderLock);
          })
          .HasValue());

  auto kernelResult = NGIN::Core::CreateKernel(MakeHostConfig(catalog));
  REQUIRE(kernelResult.HasValue());
  auto kernel = kernelResult.Value();

  REQUIRE(kernel->Start().HasValue());
  REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Running);

  const auto report = kernel->GetStartupReport();
  REQUIRE(report.resolvedModules.size() == 2);
  REQUIRE(report.failures.empty());

  REQUIRE(kernel->Shutdown().HasValue());
  REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Shutdown);

  const auto startA = std::find(order.begin(), order.end(), "start:A");
  const auto startB = std::find(order.begin(), order.end(), "start:B");
  REQUIRE(startA != order.end());
  REQUIRE(startB != order.end());
  REQUIRE(startA < startB);
}

TEST_CASE("MissingRequiredDependencyFailsBeforeLifecycle",
          "[runtime][resolver]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  auto broken = MakeDescriptor("Core.Broken");
  broken.dependencies.push_back(NGIN::Core::DependencyDescriptor{
      .name = "Core.Missing",
      .optional = false,
      .requiredVersion = Range(">=0.1.0"),
  });

  REQUIRE(
      RegisterModule(
          catalog, broken,
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Broken", nullptr, nullptr);
          })
          .HasValue());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.HasValue());
  REQUIRE(start.Error().code == NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("PlatformRangeAndDependencyVersionAreEnforced", "[runtime][compat]") {
  SECTION("Module platform range mismatch is rejected") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto incompatible = MakeDescriptor("Core.Incompatible");
    incompatible.compatiblePlatformRange = Range(">=2.0.0 <3.0.0");
    REQUIRE(
        RegisterModule(catalog, incompatible,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "Incompatible", nullptr, nullptr);
                       })
            .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.Error().code ==
            NGIN::Core::KernelErrorCode::IncompatiblePlatform);
  }

  SECTION("Module operating system mismatch is rejected") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto incompatible = MakeDescriptor("Editor.Incompatible");
    incompatible.operatingSystems = {"windows"};

    REQUIRE(
        RegisterModule(catalog, incompatible,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "EditorOnly", nullptr, nullptr);
                       })
            .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.Error().code ==
            NGIN::Core::KernelErrorCode::IncompatiblePlatform);
  }

  SECTION("Dependency requiredVersion mismatch is rejected") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto dep = MakeDescriptor("Core.Dep");
    dep.version = Version(1, 0, 0);

    auto user = MakeDescriptor("Core.User");
    user.dependencies.push_back(NGIN::Core::DependencyDescriptor{
        .name = "Core.Dep",
        .optional = false,
        .requiredVersion = Range(">=2.0.0"),
    });

    REQUIRE(
        RegisterModule(catalog, dep,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "Dep", nullptr, nullptr);
                       })
            .HasValue());
    REQUIRE(
        RegisterModule(catalog, user,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "User", nullptr, nullptr);
                       })
            .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.Error().code ==
            NGIN::Core::KernelErrorCode::IncompatibleVersion);
  }
}

TEST_CASE("LayerAndStartupStageViolationsAreRejected", "[runtime][resolver]") {
  SECTION("Forbidden family dependency fails") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto editor =
        MakeDescriptor("Editor.Tool", NGIN::Core::ModuleFamily::Editor);
    auto app = MakeDescriptor("App.Game", NGIN::Core::ModuleFamily::App);
    app.dependencies.push_back(NGIN::Core::DependencyDescriptor{
        .name = "Editor.Tool",
        .optional = false,
        .requiredVersion = Range(">=0.1.0"),
    });

    REQUIRE(
        RegisterModule(catalog, editor,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "EditorTool", nullptr, nullptr);
                       })
            .HasValue());
    REQUIRE(
        RegisterModule(catalog, app,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "AppGame", nullptr, nullptr);
                       })
            .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.Error().code ==
            NGIN::Core::KernelErrorCode::LayerConstraintViolation);
  }

  SECTION("Dependency with later startup stage fails") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto early = MakeDescriptor("Core.Early", NGIN::Core::ModuleFamily::Core,
                                NGIN::Core::StartupStage::Foundation);
    auto late = MakeDescriptor("Core.Late", NGIN::Core::ModuleFamily::Core,
                               NGIN::Core::StartupStage::Features);
    early.dependencies.push_back(NGIN::Core::DependencyDescriptor{
        .name = "Core.Late",
        .optional = false,
        .requiredVersion = Range(">=0.1.0"),
    });

    REQUIRE(
        RegisterModule(catalog, early,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "Early", nullptr, nullptr);
                       })
            .HasValue());
    REQUIRE(
        RegisterModule(catalog, late,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "Late", nullptr, nullptr);
                       })
            .HasValue());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.Error().code ==
            NGIN::Core::KernelErrorCode::StageOrderingViolation);
  }
}

TEST_CASE("LifecycleFailureUnwindsStartedModules", "[runtime][lifecycle]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  std::vector<std::string> order;
  std::mutex orderLock;

  auto a = MakeDescriptor("Core.A");
  auto b = MakeDescriptor("Core.B");
  b.dependencies.push_back(NGIN::Core::DependencyDescriptor{
      .name = "Core.A",
      .optional = false,
      .requiredVersion = Range(">=0.1.0"),
  });

  REQUIRE(
      RegisterModule(
          catalog, a,
          [&]() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "A", &order, &orderLock, HookModule::FailStage::None);
          })
          .HasValue());

  REQUIRE(
      RegisterModule(
          catalog, b,
          [&]() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "B", &order, &orderLock, HookModule::FailStage::Init);
          })
          .HasValue());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.HasValue());
  REQUIRE(start.Error().code ==
          NGIN::Core::KernelErrorCode::ModuleLifecycleFailure);
  REQUIRE(start.Error().cause != nullptr);

  REQUIRE(std::find(order.begin(), order.end(), "start:A") == order.end());
  REQUIRE(std::find(order.begin(), order.end(), "shutdown:A") != order.end());
}

TEST_CASE("ServiceRegistrySupportsSingletonScopedTransient",
          "[runtime][services]") {
  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(static_cast<bool>(registry));

  auto scopeA = registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "A");
  auto scopeB = registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "B");
  REQUIRE(scopeA.HasValue());
  REQUIRE(scopeB.HasValue());

  NGIN::UInt32 singletonCounter = 0;
  NGIN::UInt32 scopedCounter = 0;
  NGIN::UInt32 transientCounter = 0;

  REQUIRE(registry
              ->RegisterFactory<NGIN::UInt32>(
                  "Svc.Singleton",
                  [&](NGIN::Core::ServiceResolutionContext &)
                      -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::UInt32>> {
                    ++singletonCounter;
                    return NGIN::Memory::MakeShared<NGIN::UInt32>(singletonCounter);
                  },
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Singleton,
                      .ownerScope = NGIN::Core::ServiceScopeId::Global(),
                  })
              .HasValue());

  REQUIRE(registry
              ->RegisterFactory<NGIN::UInt32>(
                  "Svc.Scoped",
                  [&](NGIN::Core::ServiceResolutionContext &)
                      -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::UInt32>> {
                    ++scopedCounter;
                    return NGIN::Memory::MakeShared<NGIN::UInt32>(scopedCounter);
                  },
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Scoped,
                      .ownerScope = scopeA.Value(),
                  })
              .HasValue());

  REQUIRE(registry
              ->RegisterFactory<NGIN::UInt32>(
                  "Svc.Transient",
                  [&](NGIN::Core::ServiceResolutionContext &)
                      -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::UInt32>> {
                    ++transientCounter;
                    return NGIN::Memory::MakeShared<NGIN::UInt32>(transientCounter);
                  },
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Transient,
                      .ownerScope = scopeA.Value(),
                  })
              .HasValue());

  auto s1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Singleton");
  auto s2 = registry->ResolveRequired<NGIN::UInt32>("Svc.Singleton");
  REQUIRE(s1.HasValue());
  REQUIRE(s2.HasValue());
  REQUIRE(s1.Value().Get() == s2.Value().Get());
  REQUIRE(*s1.Value() == 1);
  REQUIRE(*s2.Value() == 1);

  auto scopedA1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Scoped", scopeA.Value());
  auto scopedA2 = registry->ResolveRequired<NGIN::UInt32>("Svc.Scoped", scopeA.Value());
  auto scopedB1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Scoped", scopeB.Value());
  REQUIRE(scopedA1.HasValue());
  REQUIRE(scopedA2.HasValue());
  REQUIRE(scopedB1.HasValue());
  REQUIRE(scopedA1.Value().Get() == scopedA2.Value().Get());
  REQUIRE(scopedB1.Value().Get() != scopedA1.Value().Get());

  auto t1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Transient", scopeA.Value());
  auto t2 = registry->ResolveRequired<NGIN::UInt32>("Svc.Transient", scopeA.Value());
  REQUIRE(t1.HasValue());
  REQUIRE(t2.HasValue());
  REQUIRE(t1.Value().Get() != t2.Value().Get());

  REQUIRE(registry->EndScope(scopeA.Value()).HasValue());
  auto scopedAfterEnd = registry->ResolveOptional<NGIN::UInt32>("Svc.Scoped", scopeA.Value());
  REQUIRE(scopedAfterEnd.HasValue());
  REQUIRE_FALSE(scopedAfterEnd.Value().has_value());
}

TEST_CASE("ServiceRegistryAutoConstructsLazyServices",
          "[runtime][services]") {
  AutoConstructedService::constructed = 0;

  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(static_cast<bool>(registry));

  REQUIRE(registry->RegisterSingleton<AutoConstructedService>().HasValue());
  REQUIRE(AutoConstructedService::constructed == 0);

  auto first = registry->ResolveRequired<AutoConstructedService>();
  auto second = registry->ResolveRequired<AutoConstructedService>();
  REQUIRE(first.HasValue());
  REQUIRE(second.HasValue());
  REQUIRE(AutoConstructedService::constructed == 1);
  REQUIRE(first.Value().Get() == second.Value().Get());

  auto scopeA =
      registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "Auto.A");
  auto scopeB =
      registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "Auto.B");
  REQUIRE(scopeA.HasValue());
  REQUIRE(scopeB.HasValue());

  REQUIRE(registry
              ->RegisterScoped<ProviderDependencyService>(
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Scoped,
                      .ownerScope = scopeA.Value(),
                  })
              .HasValue());
  REQUIRE(registry
              ->RegisterTransient<ProviderConsumerService>(
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Transient,
                      .ownerScope = scopeA.Value(),
                  })
              .HasValue());

  ProviderDependencyService::constructed = 0;
  ProviderConsumerService::constructed = 0;

  auto consumerA1 =
      registry->ResolveRequired<ProviderConsumerService>(scopeA.Value());
  auto consumerA2 =
      registry->ResolveRequired<ProviderConsumerService>(scopeA.Value());
  REQUIRE(consumerA1.HasValue());
  REQUIRE(consumerA2.HasValue());
  REQUIRE(consumerA1.Value().Get() != consumerA2.Value().Get());
  REQUIRE(ProviderConsumerService::constructed == 2);
  REQUIRE(ProviderDependencyService::constructed == 1);
  REQUIRE(consumerA1.Value()->dependency.Get() ==
          consumerA2.Value()->dependency.Get());
  REQUIRE(consumerA1.Value()->dependency->value == 7);

  auto consumerB =
      registry->ResolveRequired<ProviderConsumerService>(scopeB.Value());
  REQUIRE(consumerB.HasValue());
  REQUIRE(ProviderDependencyService::constructed == 2);
  REQUIRE(consumerB.Value()->dependency.Get() !=
          consumerA1.Value()->dependency.Get());
}

TEST_CASE("ServiceRegistryReportsTypedResolutionErrors",
          "[runtime][services]") {
  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(static_cast<bool>(registry));

  REQUIRE(registry
              ->RegisterSingletonValue<std::string>("Svc.Message", "hello")
              .HasValue());

  auto wrongType = registry->ResolveRequired<NGIN::UInt32>("Svc.Message");
  REQUIRE_FALSE(wrongType.HasValue());
  REQUIRE(wrongType.Error().code ==
          NGIN::Core::KernelErrorCode::InvalidArgument);

  auto missingOptional = registry->ResolveOptional<std::string>("Svc.Missing");
  REQUIRE(missingOptional.HasValue());
  REQUIRE_FALSE(missingOptional.Value().has_value());

  auto missingRequired = registry->ResolveRequired<std::string>("Svc.Missing");
  REQUIRE_FALSE(missingRequired.HasValue());
  REQUIRE(missingRequired.Error().code ==
          NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("ModuleRequiredServiceContractsAreEnforcedBeforeInit",
          "[runtime][services]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  auto consumer = MakeDescriptor("Core.Consumer");
  consumer.requiresServices.push_back("Service.Required");

  REQUIRE(
      RegisterModule(
          catalog, consumer,
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Consumer", nullptr, nullptr);
          })
          .HasValue());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.HasValue());
  REQUIRE(start.Error().code ==
          NGIN::Core::KernelErrorCode::MissingRequiredDependency);
}

TEST_CASE("HostConfigSourcesCliAndEnvironmentAreApplied", "[runtime][config]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  auto desc = MakeDescriptor("Core.ConfigModule");
  REQUIRE(
      RegisterModule(
          catalog, desc,
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "ConfigModule", nullptr, nullptr);
          })
          .HasValue());

  const auto tempRoot = MakeTempDir("ngin-runtime-tests-config");
  const auto configPath = tempRoot.Join("base.cfg");
  WriteTextFile(configPath, "App.Value=file\nApp.Other=from_file\n");

  auto cfg = MakeHostConfig(catalog);
  cfg.workingDirectory = ToString(tempRoot);
  cfg.environmentName = "TestEnv";
  cfg.configSources = {"base.cfg"};
  cfg.commandLineArgs = {"--App.Value=cli"};

  auto kernel = NGIN::Core::CreateKernel(cfg).Value();
  REQUIRE(kernel->Start().HasValue());

  auto config = kernel->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("App.Value").Value() == "cli");
  REQUIRE(config->GetRaw("App.Other").Value() == "from_file");
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").Value() == "TestEnv");
  REQUIRE(config->GetRaw("Kernel.HostName").Value() == "Core.Tests");

  REQUIRE(kernel->Shutdown().HasValue());
  RemovePath(tempRoot);
}

TEST_CASE("DynamicDescriptorDiscoveryUsesPluginSearchPaths",
          "[runtime][plugin]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  const auto root = MakeTempDir("ngin-runtime-tests-plugins");
  const auto descriptorPath = root.Join("DemoPlugin").Join("demo.module.xml");
  WriteTextFile(descriptorPath,
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<Module Name=\"Core.DynamicDemo\"\n"
                "        Family=\"Core\"\n"
                "        Type=\"Runtime\"\n"
                "        StartupStage=\"Services\"\n"
                "        Version=\"0.1.0\"\n"
                "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\"\n"
                "        ReflectionRequired=\"false\">\n"
                "  <Compatibility>\n"
                "    <OperatingSystems>\n"
                "      <OperatingSystem Name=\"linux\" />\n"
                "      <OperatingSystem Name=\"windows\" />\n"
                "      <OperatingSystem Name=\"macos\" />\n"
                "    </OperatingSystems>\n"
                "  </Compatibility>\n"
                "  <Dependencies />\n"
                "  <RequiresServices />\n"
                "  <ProvidesServices />\n"
                "  <Capabilities />\n"
                "</Module>\n");

  auto cfg = MakeHostConfig(catalog);
  cfg.enableDynamicPlugins = true;
  cfg.pluginSearchPaths = {ToString(root)};

  auto kernel = NGIN::Core::CreateKernel(cfg).Value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.HasValue());
  REQUIRE(start.Error().code ==
          NGIN::Core::KernelErrorCode::DynamicPluginUnsupported);
  RemovePath(root);
}

TEST_CASE("DynamicDescriptorDiscoveryCanUseInjectedFilesystem",
          "[runtime][plugin][filesystem]") {
  const auto realRoot = MakeTempDir("ngin-runtime-tests-plugin-vfs");
  WriteTextFile(realRoot.Join("DemoPlugin").Join("demo.module.xml"),
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<Module Name=\"Core.DynamicDemo.Virtual\"\n"
                "        Family=\"Core\"\n"
                "        Type=\"Runtime\"\n"
                "        StartupStage=\"Services\"\n"
                "        Version=\"0.1.0\"\n"
                "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\"\n"
                "        ReflectionRequired=\"false\">\n"
                "  <Compatibility>\n"
                "    <OperatingSystems>\n"
                "      <OperatingSystem Name=\"linux\" />\n"
                "      <OperatingSystem Name=\"windows\" />\n"
                "      <OperatingSystem Name=\"macos\" />\n"
                "    </OperatingSystems>\n"
                "  </Compatibility>\n"
                "  <Dependencies />\n"
                "  <RequiresServices />\n"
                "  <ProvidesServices />\n"
                "  <Capabilities />\n"
                "</Module>\n");

  auto fileSystem = NGIN::Memory::MakeSharedAs<NGIN::IO::IFileSystem,
                                               NGIN::IO::LocalFileSystem>();
  auto pluginCatalog =
      NGIN::Memory::MakeSharedAs<NGIN::Core::IPluginCatalog,
                                 NGIN::Core::FilesystemPluginCatalog>(
          std::vector<std::string>{ToString(realRoot)}, fileSystem);

  std::vector<NGIN::Core::ModuleDescriptor> descriptors{};
  auto collect = pluginCatalog->CollectDescriptors(descriptors);
  REQUIRE(collect.HasValue());
  REQUIRE(descriptors.size() == 1);
  REQUIRE(descriptors.front().name == "Core.DynamicDemo.Virtual");
  REQUIRE(descriptors.front().pluginName == "DemoPlugin");
  RemovePath(realRoot);
}

TEST_CASE("TaskLanesAndBarriersExecutePerLane", "[runtime][tasks]") {
  auto runtime = NGIN::Core::CreateTaskRuntime(2, true);
  REQUIRE(static_cast<bool>(runtime));

  REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Main));
  REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::IO));
  REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Worker));
  REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Background));
  REQUIRE(runtime->IsLaneEnabled(NGIN::Core::TaskLane::Render));

  std::atomic<NGIN::UInt32> hits{0};
  REQUIRE(runtime
              ->Submit(NGIN::Core::TaskLane::IO,
                       [&]() { hits.fetch_add(1, std::memory_order_relaxed); })
              .HasValue());
  REQUIRE(runtime
              ->Submit(NGIN::Core::TaskLane::Worker,
                       [&]() { hits.fetch_add(1, std::memory_order_relaxed); })
              .HasValue());
  REQUIRE(runtime
              ->Submit(NGIN::Core::TaskLane::Render,
                       [&]() { hits.fetch_add(1, std::memory_order_relaxed); })
              .HasValue());

  REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::IO).HasValue());
  REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::Worker).HasValue());
  REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::Render).HasValue());
  REQUIRE(hits.load(std::memory_order_relaxed) == 3);
}

TEST_CASE("TypedEventBusPublishSubscribeDeliversMetadata",
          "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  NGIN::UInt32 seenValue = 0;
  NGIN::Core::EventMetadata metadata{};
  auto sub = bus->Subscribe<TestUserEvent>(
      [&](const NGIN::Core::TypedEventRecord<TestUserEvent> &record) {
        seenValue = record.event.value;
        metadata = record.metadata;
      },
      NGIN::Core::EventScope{.owner = "tests"});
  REQUIRE(sub.HasValue());

  REQUIRE(bus->Publish(TestUserEvent{.value = 7}).HasValue());

  REQUIRE(seenValue == 7);
  REQUIRE(metadata.channel == NGIN::Core::EventChannelName<TestUserEvent>());
  REQUIRE(metadata.channelId == NGIN::Core::EventChannelId<TestUserEvent>());
  REQUIRE(metadata.sequence > 0);
  REQUIRE(metadata.queue == NGIN::Core::EventQueue::Main);
  REQUIRE(metadata.reserved == NGIN::Core::ReservedKernelEvent::None);
}

TEST_CASE("TypedEventBusDeferredQueuesFlushByQueueAndType",
          "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  std::vector<NGIN::UInt32> seen;
  auto sub = bus->Subscribe<TestUserEvent>(
      [&](const NGIN::Core::TypedEventRecord<TestUserEvent> &record) {
        seen.push_back(record.event.value);
      },
      NGIN::Core::EventScope{.owner = "tests"});
  REQUIRE(sub.HasValue());

  REQUIRE(
      bus->EnqueueTo(NGIN::Core::EventQueue::Main, TestUserEvent{.value = 1})
          .HasValue());
  REQUIRE(bus->EnqueueTo(NGIN::Core::EventQueue::IO, TestUserEvent{.value = 2})
              .HasValue());

  REQUIRE(bus->FlushFrom<TestUserEvent>(NGIN::Core::EventQueue::IO).HasValue());
  REQUIRE(seen == std::vector<NGIN::UInt32>{2});

  REQUIRE(bus->Flush<TestUserEvent>().HasValue());
  REQUIRE(seen == std::vector<NGIN::UInt32>{2, 1});
}

TEST_CASE("TypedEventBusRespectsPriorityUnsubscribeAndScopeClear",
          "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  std::vector<std::string> order;
  auto low = bus->Subscribe<TestScopedEvent>(
      [&](const NGIN::Core::TypedEventRecord<TestScopedEvent> &record) {
        order.push_back(record.event.name);
      },
      NGIN::Core::EventScope{.owner = "scope.low"}, 1);
  REQUIRE(low.HasValue());

  auto high = bus->Subscribe<TestScopedEvent>(
      [&](const NGIN::Core::TypedEventRecord<TestScopedEvent> &record) {
        order.push_back(record.event.name + "-high");
      },
      NGIN::Core::EventScope{.owner = "scope.high"}, 5);
  REQUIRE(high.HasValue());

  REQUIRE(bus->Publish(TestScopedEvent{.name = "first"}).HasValue());
  REQUIRE(order == std::vector<std::string>{"first-high", "first"});

  REQUIRE(bus->Unsubscribe(high.Value()).HasValue());
  order.clear();
  REQUIRE(bus->Publish(TestScopedEvent{.name = "second"}).HasValue());
  REQUIRE(order == std::vector<std::string>{"second"});

  bus->ClearScope(NGIN::Core::EventScope{.owner = "scope.low"});
  order.clear();
  REQUIRE(bus->Publish(TestScopedEvent{.name = "third"}).HasValue());
  REQUIRE(order.empty());
}

TEST_CASE("EventBusInteropBetweenTypedAndRawSubscribers", "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  NGIN::UInt32 typedValue = 0;
  auto typedSub = bus->Subscribe<TestUserEvent>(
      [&](const NGIN::Core::TypedEventRecord<TestUserEvent> &record) {
        typedValue = record.event.value;
      },
      NGIN::Core::EventScope{.owner = "typed"});
  REQUIRE(typedSub.HasValue());

  REQUIRE(
      bus
          ->PublishRawImmediate(NGIN::Core::RawEventRecord{
              .channel =
                  std::string(NGIN::Core::EventChannelName<TestUserEvent>()),
              .payload = NGIN::Utilities::Any<>(TestUserEvent{.value = 42})})
          .HasValue());
  REQUIRE(typedValue == 42);

  std::vector<NGIN::UInt32> rawValues;
  auto rawSub = bus->SubscribeRaw(
      std::string(NGIN::Core::EventChannelName<TestUserEvent>()),
      [&](const NGIN::Core::RawEventRecord &record) {
        const auto *typed = record.payload.template TryCast<TestUserEvent>();
        REQUIRE(typed != nullptr);
        rawValues.push_back(typed->value);
      },
      NGIN::Core::EventScope{.owner = "raw"});
  REQUIRE(rawSub.HasValue());

  REQUIRE(bus->Publish(TestUserEvent{.value = 99}).HasValue());
  REQUIRE(rawValues == std::vector<NGIN::UInt32>{99});
}

TEST_CASE("TypedEventSubscribersRejectPayloadMismatch", "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  auto sub = bus->Subscribe<TestUserEvent>(
      [](const NGIN::Core::TypedEventRecord<TestUserEvent> &) {},
      NGIN::Core::EventScope{.owner = "tests"});
  REQUIRE(sub.HasValue());

  auto publish = bus->PublishRawImmediate(NGIN::Core::RawEventRecord{
      .channel = std::string(NGIN::Core::EventChannelName<TestUserEvent>()),
      .payload = NGIN::Utilities::Any<>(std::string("wrong-type"))});
  REQUIRE_FALSE(publish.HasValue());
  REQUIRE(publish.Error().code ==
          NGIN::Core::KernelErrorCode::EventDispatchFailure);
}

TEST_CASE("DeferredEventSequenceIsPreservedOnFlush", "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  NGIN::UInt64 seenSequence = 0;
  auto sub = bus->SubscribeRaw(
      std::string(NGIN::Core::EventChannelName<TestUserEvent>()),
      [&](const NGIN::Core::RawEventRecord &record) {
        seenSequence = record.sequence;
      },
      NGIN::Core::EventScope{.owner = "tests"});
  REQUIRE(sub.HasValue());

  REQUIRE(bus
              ->EnqueueRaw(NGIN::Core::RawEventRecord{
                  .channel = std::string(
                      NGIN::Core::EventChannelName<TestUserEvent>()),
                  .payload = NGIN::Utilities::Any<>(TestUserEvent{.value = 1}),
                  .sequence = 77})
              .HasValue());

  REQUIRE(bus->FlushRawFrom(NGIN::Core::EventQueue::Main).HasValue());
  REQUIRE(seenSequence == 77);
}

TEST_CASE("KernelEmitsReservedLifecycleEvents", "[runtime][events]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  std::vector<std::string> typedEvents;
  std::vector<std::string> rawEvents;
  std::mutex typedEventsLock;
  std::mutex rawEventsLock;

  auto desc = MakeDescriptor("Core.EventProbe.Typed");
  REQUIRE(
      RegisterModule(catalog, desc,
                     [&]() -> NGIN::Core::CoreResult<
                               NGIN::Memory::Shared<NGIN::Core::IModule>> {
                       return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                         EventProbeModule>(
                           &typedEvents, &typedEventsLock);
                     })
          .HasValue());

  auto rawDesc = MakeDescriptor("Core.EventProbe.Raw");
  REQUIRE(
      RegisterModule(catalog, rawDesc,
                     [&]() -> NGIN::Core::CoreResult<
                               NGIN::Memory::Shared<NGIN::Core::IModule>> {
                       return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                         RawEventProbeModule>(
                           &rawEvents, &rawEventsLock);
                     })
          .HasValue());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
  REQUIRE(kernel->Start().HasValue());
  REQUIRE(kernel->Shutdown().HasValue());

  REQUIRE(std::find(typedEvents.begin(), typedEvents.end(), "ModuleLoaded") !=
          typedEvents.end());
  REQUIRE(std::find(typedEvents.begin(), typedEvents.end(), "ModuleStarted") !=
          typedEvents.end());
  REQUIRE(std::find(typedEvents.begin(), typedEvents.end(), "KernelRunning") !=
          typedEvents.end());
  REQUIRE(std::find(typedEvents.begin(), typedEvents.end(), "KernelStopping") !=
          typedEvents.end());

  REQUIRE(std::find(rawEvents.begin(), rawEvents.end(), "ModuleLoaded") !=
          rawEvents.end());
  REQUIRE(std::find(rawEvents.begin(), rawEvents.end(), "ModuleStarted") !=
          rawEvents.end());
  REQUIRE(std::find(rawEvents.begin(), rawEvents.end(), "KernelRunning") !=
          rawEvents.end());
  REQUIRE(std::find(rawEvents.begin(), rawEvents.end(), "KernelStopping") !=
          rawEvents.end());
}

TEST_CASE("KernelEmitsTypedConfigChangedEventAndRawCompatibility",
          "[runtime][events][config]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).Value();
  REQUIRE(kernel->Start().HasValue());

  auto events = kernel->GetEvents();
  auto config = kernel->GetConfig();
  REQUIRE(static_cast<bool>(events));
  REQUIRE(static_cast<bool>(config));

  std::vector<std::string> typedChanges;
  auto typedSub = events->Subscribe<NGIN::Core::ConfigChangeEvent>(
      [&](const NGIN::Core::TypedEventRecord<NGIN::Core::ConfigChangeEvent>
              &record) {
        typedChanges.push_back(record.event.key + "=" + record.event.newValue);
        REQUIRE(record.metadata.channel == "ConfigChanged");
        REQUIRE(record.metadata.reserved ==
                NGIN::Core::ReservedKernelEvent::ConfigChanged);
      },
      NGIN::Core::EventScope{.owner = "typed"});
  REQUIRE(typedSub.HasValue());

  std::vector<std::string> rawChanges;
  auto rawSub = events->SubscribeRaw(
      "ConfigChanged",
      [&](const NGIN::Core::RawEventRecord &record) {
        rawChanges.push_back(record.channel);
        const auto *change =
            record.payload.template TryCast<NGIN::Core::ConfigChangeEvent>();
        REQUIRE(change != nullptr);
        REQUIRE(change->key == "App.Mode");
        REQUIRE(change->newValue == "runtime");
      },
      NGIN::Core::EventScope{.owner = "raw"});
  REQUIRE(rawSub.HasValue());

  REQUIRE(config
              ->SetValue(NGIN::Core::ConfigLayer::RuntimeMutable, "App.Mode",
                         "runtime")
              .HasValue());

  REQUIRE(typedChanges == std::vector<std::string>{"App.Mode=runtime"});
  REQUIRE(rawChanges == std::vector<std::string>{"ConfigChanged"});
  REQUIRE(kernel->Shutdown().HasValue());
}

TEST_CASE("ThreadPoliciesAreEnforced", "[runtime][threading]") {
  SECTION("SingleThreadOnly rejects non-owner thread") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto cfg = MakeHostConfig(catalog);
    cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::SingleThreadOnly;

    auto kernel = NGIN::Core::CreateKernel(cfg).Value();

    std::promise<NGIN::Core::CoreResult<void>> promise;
    auto future = promise.get_future();
    std::thread worker([&]() mutable { promise.set_value(kernel->Start()); });
    worker.join();

    auto start = future.get();
    REQUIRE_FALSE(start.HasValue());
    REQUIRE(start.Error().code ==
            NGIN::Core::KernelErrorCode::ThreadPolicyViolation);
  }

  SECTION("Serialized allows cross-thread run and RequestStop") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto desc = MakeDescriptor("Core.Runner");
    REQUIRE(
        RegisterModule(catalog, desc,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "Runner", nullptr, nullptr);
                       })
            .HasValue());

    auto cfg = MakeHostConfig(catalog);
    cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::Serialized;
    auto kernel = NGIN::Core::CreateKernel(cfg).Value();

    std::promise<NGIN::Core::CoreResult<void>> promise;
    auto future = promise.get_future();
    std::thread runner([&]() mutable { promise.set_value(kernel->Run()); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    kernel->RequestStop("test");

    runner.join();
    auto result = future.get();
    REQUIRE(result.HasValue());
    REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Shutdown);
  }
}

TEST_CASE("ApplicationBuilderBuildsHostFromCode", "[builder][host]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.Tests");
  builder->SetConfiguration("Builder.Target");
  builder->Services().AddDefaults().AddConfiguration().AddSingletonValue<std::string>(
      "App.Message", "hello-builder");
  builder->Modules()
      .Register(MakeRegistration(
          MakeDescriptor("App.Builder", NGIN::Core::ModuleFamily::App,
                         NGIN::Core::StartupStage::Features),
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Builder", nullptr, nullptr);
          }))
      .Enable("App.Builder");

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(app.Value()->GetConfigurationName() == "Builder.Target");

  auto report = app.Value()->GetStartupReport();
  REQUIRE(report.targetName == "Builder.Target");
  REQUIRE(report.hostName == "Builder.Tests");
  REQUIRE(report.hostType == "ConsoleApp");

  REQUIRE(app.Value()->Start().HasValue());

  auto services = app.Value()->GetServices();
  REQUIRE(static_cast<bool>(services));

  auto resolved = services->ResolveRequired<std::string>("App.Message");
  REQUIRE(resolved.HasValue());
  REQUIRE(*resolved.Value() == "hello-builder");

  auto config = app.Value()->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("Kernel.TargetName").HasValue());
  REQUIRE(config->GetRaw("Kernel.TargetName").Value() == "Builder.Target");

  auto secondBuild = builder->Build();
  REQUIRE_FALSE(secondBuild.HasValue());
  REQUIRE(secondBuild.Error().code ==
          NGIN::Core::KernelErrorCode::InvalidState);

  REQUIRE(app.Value()->Shutdown().HasValue());
}

TEST_CASE("ApplicationBuilderLoadsProjectManifestAndConfig",
          "[builder][manifest]") {
  const auto tempDir = MakeTempDir("ngin-core-builder-manifest");

  WriteTextFile(tempDir.Join("app.cfg"), "App.Mode=manifest\n");
  WriteTextFile(tempDir.Join("Manifest.Tests.nginproj"),
                R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="Manifest.Tests"
         Type="Application"
         DefaultConfiguration="Samples.Manifest">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable"
          Name="Manifest.Tests"
          Target="Manifest.Tests" />
  <Build Backend="CMake"
         Mode="Generated"
         Language="CXX"
         LanguageStandard="23">
    <CompileDefinitions>
      <Definition Value="MANIFEST_TESTS=1" Visibility="Private" />
    </CompileDefinitions>
  </Build>
  <References>
    <Package Name="NGIN.ECS" Version=">=0.1.0 &lt;1.0.0" />
  </References>
  <ConfigSources>
    <Config Source="app.cfg" />
  </ConfigSources>
  <Environments>
    <Environment Name="Dev" />
  </Environments>
  <Runtime>
    <EnableModules>
      <ModuleRef Name="App.Manifest" />
    </EnableModules>
    <DisableModules>
      <ModuleRef Name="App.Disabled" />
    </DisableModules>
  </Runtime>
  <Configurations>
    <Configuration Name="Samples.Manifest"
             BuildConfiguration="Debug"
             OperatingSystem="linux"
             Architecture="x64"
             Environment="Dev">
      <Launch Executable="Manifest.Tests" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
)");

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->UseProjectFile(ToString(tempDir.Join("Manifest.Tests.nginproj")));
  builder->Services().AddConfiguration();
  builder->Modules()
      .Register(MakeRegistration(
          MakeDescriptor("App.Manifest", NGIN::Core::ModuleFamily::App,
                         NGIN::Core::StartupStage::Features),
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Manifest", nullptr, nullptr);
          }))
      .Register(MakeRegistration(
          MakeDescriptor("App.Disabled", NGIN::Core::ModuleFamily::App,
                         NGIN::Core::StartupStage::Features),
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Disabled", nullptr, nullptr);
          }));

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(app.Value()->Start().HasValue());

  auto report = app.Value()->GetStartupReport();
  REQUIRE(report.targetName == "Samples.Manifest");
  REQUIRE(std::find(report.resolvedPackages.begin(),
                    report.resolvedPackages.end(),
                    "NGIN.ECS") != report.resolvedPackages.end());
  REQUIRE(std::find(report.resolvedModules.begin(),
                    report.resolvedModules.end(),
                    "App.Manifest") != report.resolvedModules.end());
  REQUIRE(std::find(report.resolvedModules.begin(),
                    report.resolvedModules.end(),
                    "App.Disabled") == report.resolvedModules.end());

  auto config = app.Value()->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("App.Mode").HasValue());
  REQUIRE(config->GetRaw("App.Mode").Value() == "manifest");
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").HasValue());
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").Value() == "Dev");

  REQUIRE(app.Value()->Shutdown().HasValue());
  RemovePath(tempDir);
}

TEST_CASE("ApplicationBuilderLoadsProjectManifestFromInjectedFilesystem",
          "[builder][manifest][filesystem]") {
  const auto realRoot = MakeTempDir("ngin-core-builder-vfs");
  WriteTextFile(realRoot.Join("app.cfg"), "App.Mode=virtual\n");
  WriteTextFile(realRoot.Join("Virtual.Manifest.nginproj"),
                R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="Virtual.Manifest"
         Type="Application"
         DefaultConfiguration="Samples.Virtual">
  <Output Kind="Executable"
          Name="Virtual.Manifest"
          Target="Virtual.Manifest" />
  <ConfigSources>
    <Config Source="app.cfg" />
  </ConfigSources>
  <Environments>
    <Environment Name="Virtual" />
  </Environments>
  <Runtime>
    <EnableModules>
      <ModuleRef Name="App.VirtualManifest" />
    </EnableModules>
  </Runtime>
  <Configurations>
    <Configuration Name="Samples.Virtual"
             BuildConfiguration="Debug"
             OperatingSystem="linux"
             Architecture="x64"
             Environment="Virtual">
      <Launch Executable="Virtual.Manifest" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
)");

  auto fileSystem =
      MakeMountedVirtualFileSystem(realRoot, NGIN::IO::Path{"/virtual"});
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->UseFileSystem(fileSystem);
  builder->UseProjectFile("/virtual/Virtual.Manifest.nginproj");
  builder->Services().AddConfiguration();
  builder->Modules()
      .Register(MakeRegistration(
          MakeDescriptor("App.VirtualManifest", NGIN::Core::ModuleFamily::App,
                         NGIN::Core::StartupStage::Features),
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "VirtualManifest", nullptr, nullptr);
          }))
      .Enable("App.VirtualManifest");

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(app.Value()->Start().HasValue());

  auto config = app.Value()->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("App.Mode").HasValue());
  REQUIRE(config->GetRaw("App.Mode").Value() == "virtual");
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").Value() == "Virtual");

  REQUIRE(app.Value()->Shutdown().HasValue());
  RemovePath(realRoot);
}

TEST_CASE("ApplicationBuilderTargetOverrideBeatsProjectDefault",
          "[builder][manifest]") {
  const auto tempDir = MakeTempDir("ngin-core-builder-target");

  WriteTextFile(tempDir.Join("Manifest.Override.nginproj"),
                R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="Manifest.Override"
         Type="Application"
         DefaultConfiguration="Default.Target">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable"
          Name="Manifest.Override"
          Target="Manifest.Override" />
  <Environments>
    <Environment Name="Default" />
    <Environment Name="Override" />
  </Environments>
  <Configurations>
    <Configuration Name="Default.Target"
             BuildConfiguration="Debug"
             OperatingSystem="linux"
             Architecture="x64"
             Environment="Default">
      <Launch Executable="Manifest.Override" WorkingDirectory="." />
      <Runtime>
        <EnableModules>
          <ModuleRef Name="App.Default" />
        </EnableModules>
      </Runtime>
    </Configuration>
    <Configuration Name="Override.Target"
             BuildConfiguration="Release"
             OperatingSystem="linux"
             Architecture="x64"
             Environment="Override">
      <Launch Executable="Manifest.Override" WorkingDirectory="." />
      <Runtime>
        <EnableModules>
          <ModuleRef Name="App.Override" />
        </EnableModules>
      </Runtime>
    </Configuration>
  </Configurations>
</Project>
)");

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->UseProjectFile(ToString(tempDir.Join("Manifest.Override.nginproj")));
  builder->SetConfiguration("Override.Target");
  builder->Modules()
      .Register(MakeRegistration(
          MakeDescriptor("App.Default", NGIN::Core::ModuleFamily::App,
                         NGIN::Core::StartupStage::Features),
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Default", nullptr, nullptr);
          }))
      .Register(MakeRegistration(
          MakeDescriptor("App.Override", NGIN::Core::ModuleFamily::App,
                         NGIN::Core::StartupStage::Features),
          []() -> NGIN::Core::CoreResult<
                   NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "Override", nullptr, nullptr);
          }));

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(app.Value()->Start().HasValue());

  auto report = app.Value()->GetStartupReport();
  REQUIRE(report.targetName == "Override.Target");
  REQUIRE(std::find(report.resolvedModules.begin(),
                    report.resolvedModules.end(),
                    "App.Override") != report.resolvedModules.end());
  REQUIRE(std::find(report.resolvedModules.begin(),
                    report.resolvedModules.end(),
                    "App.Default") == report.resolvedModules.end());

  auto config = app.Value()->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").HasValue());
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").Value() == "Override");

  REQUIRE(app.Value()->Shutdown().HasValue());
  RemovePath(tempDir);
}

TEST_CASE("ApplicationBuilderRejectsUnknownTarget", "[builder][manifest]") {
  const auto tempDir = MakeTempDir("ngin-core-builder-missing-target");

  WriteTextFile(tempDir.Join("Manifest.Invalid.nginproj"),
                R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2"
         Name="Manifest.Invalid"
         Type="Application"
         DefaultConfiguration="Samples.Default">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable"
          Name="Manifest.Invalid"
          Target="Manifest.Invalid" />
  <Environments>
    <Environment Name="Default" />
  </Environments>
  <Configurations>
    <Configuration Name="Samples.Default"
             BuildConfiguration="Debug"
             OperatingSystem="linux"
             Architecture="x64"
             Environment="Default">
      <Launch Executable="Manifest.Invalid" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
)");

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->UseProjectFile(ToString(tempDir.Join("Manifest.Invalid.nginproj")));
  builder->SetConfiguration("Missing.Target");

  auto app = builder->Build();
  REQUIRE_FALSE(app.HasValue());
  REQUIRE(app.Error().code == NGIN::Core::KernelErrorCode::NotFound);

  RemovePath(tempDir);
}

TEST_CASE("ApplicationBuilderExecutesExplicitPackageBootstrapFromManifestFile",
          "[builder][bootstrap]") {
  const auto tempDir = MakeTempDir("ngin-core-builder-bootstrap");

  WriteTextFile(tempDir.Join("package.cfg"), "Package.Mode=bootstrapped\n");
  WriteTextFile(tempDir.Join("Samples.Package.nginpkg"),
                R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="1"
         Name="Samples.Package"
         Version="0.1.0"
         CompatiblePlatformRange=">=0.1.0 &lt;1.0.0">
  <Compatibility>
    <OperatingSystems>
      <OperatingSystem Name="linux" />
      <OperatingSystem Name="windows" />
      <OperatingSystem Name="macos" />
    </OperatingSystems>
  </Compatibility>
  <Dependencies />
  <Bootstrap Mode="BuilderHookV1"
             EntryPoint="NGIN_Bootstrap_Samples_Package"
             AutoApply="false" />
  <Plugins />
</Package>
)");

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.Package");
  builder->SetConfiguration("Builder.Package.Target");
  builder->Configuration().SetWorkingDirectory(ToString(tempDir));
  builder->Packages()
      .Add({
          .name = "Samples.Package",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = false,
      })
      .AddManifestFile(ToString(tempDir.Join("Samples.Package.nginpkg")))
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_Package)
      .ApplyBootstrap("Samples.Package");

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(app.Value()->Start().HasValue());

  auto services = app.Value()->GetServices();
  REQUIRE(static_cast<bool>(services));

  auto message = services->ResolveRequired<std::string>("Samples.Package.Message");
  REQUIRE(message.HasValue());
  REQUIRE(*message.Value() == "bootstrapped");

  auto config = app.Value()->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("Package.Mode").HasValue());
  REQUIRE(config->GetRaw("Package.Mode").Value() == "bootstrapped");

  auto report = app.Value()->GetStartupReport();
  REQUIRE(ContainsString(report.resolvedPackages, "Samples.Package"));

  REQUIRE(app.Value()->Shutdown().HasValue());
  RemovePath(tempDir);
}

TEST_CASE("ApplicationBuilderExecutesNamedPackageBootstrapEntry",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.NamedBootstrap");
  builder->SetConfiguration("Builder.NamedBootstrap.Target");
  builder->Packages()
      .Add({
          .name = "Samples.Package",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = false,
      })
      .AddManifest(NGIN::Core::PackageManifest{
          .schemaVersion = 1,
          .name = "Samples.Package",
          .version = "0.1.0",
          .compatiblePlatformRange = ">=0.1.0 <1.0.0",
          .operatingSystems = {"linux", "windows", "macos"},
          .dependencies = {},
          .bootstrap =
              NGIN::Core::PackageBootstrapDescriptor{
                  .mode = NGIN::Core::PackageBootstrapMode::BuilderHookV1,
                  .entryPoint = "NGIN_Bootstrap_Samples_Package",
                  .autoApply = false,
              },
          .contents = {},
          .modules = {},
          .plugins = {},
      })
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_Package)
      .ApplyBootstrap("Samples.Package", "NGIN_Bootstrap_Samples_PackageAlt");

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(app.Value()->Start().HasValue());

  auto services = app.Value()->GetServices();
  REQUIRE(static_cast<bool>(services));

  auto message = services->ResolveRequired<std::string>("Samples.Package.Message");
  REQUIRE(message.HasValue());
  REQUIRE(*message.Value() == "bootstrapped-alt");

  REQUIRE(app.Value()->Shutdown().HasValue());
}

TEST_CASE("ApplicationBuilderAutoAppliesPackagesInDependencyOrder",
          "[builder][bootstrap]") {
  std::vector<std::string> order{};
  g_packageBootstrapOrder = &order;

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.AutoApply");
  builder->SetConfiguration("Builder.AutoApply.Target");
  builder->Packages()
      .Add({
          .name = "Samples.PackageB",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = false,
      })
      .Add({
          .name = "Samples.PackageA",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = false,
      })
      .AddManifest(NGIN::Core::PackageManifest{
          .schemaVersion = 1,
          .name = "Samples.PackageA",
          .version = "0.1.0",
          .compatiblePlatformRange = ">=0.1.0 <1.0.0",
          .operatingSystems = {"linux", "windows", "macos"},
          .dependencies = {},
          .bootstrap =
              NGIN::Core::PackageBootstrapDescriptor{
                  .mode = NGIN::Core::PackageBootstrapMode::BuilderHookV1,
                  .entryPoint = "NGIN_Bootstrap_Samples_PackageA",
                  .autoApply = true,
              },
          .contents = {},
          .modules = {},
          .plugins = {},
      })
      .AddManifest(NGIN::Core::PackageManifest{
          .schemaVersion = 1,
          .name = "Samples.PackageB",
          .version = "0.1.0",
          .compatiblePlatformRange = ">=0.1.0 <1.0.0",
          .operatingSystems = {"linux", "windows", "macos"},
          .dependencies =
              {
                  {
                      .name = "Samples.PackageA",
                      .versionRange = ">=0.1.0 <1.0.0",
                      .optional = false,
                  },
              },
          .bootstrap =
              NGIN::Core::PackageBootstrapDescriptor{
                  .mode = NGIN::Core::PackageBootstrapMode::BuilderHookV1,
                  .entryPoint = "NGIN_Bootstrap_Samples_PackageB",
                  .autoApply = true,
              },
          .contents = {},
          .modules = {},
          .plugins = {},
      })
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_PackageB)
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_PackageA);

  auto app = builder->Build();
  REQUIRE(app.HasValue());
  REQUIRE(order ==
          std::vector<std::string>{"Samples.PackageA", "Samples.PackageB"});

  g_packageBootstrapOrder = nullptr;
}

TEST_CASE("ApplicationBuilderFailsOnMissingRequiredAutoAppliedPackageBootstrap",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.RequiredFailure");
  builder->SetConfiguration("Builder.RequiredFailure.Target");
  builder->Packages()
      .Add({
          .name = "Samples.RequiredPackage",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = false,
      })
      .AddManifest(NGIN::Core::PackageManifest{
          .schemaVersion = 1,
          .name = "Samples.RequiredPackage",
          .version = "0.1.0",
          .compatiblePlatformRange = ">=0.1.0 <1.0.0",
          .operatingSystems = {"linux", "windows", "macos"},
          .dependencies = {},
          .bootstrap =
              NGIN::Core::PackageBootstrapDescriptor{
                  .mode = NGIN::Core::PackageBootstrapMode::BuilderHookV1,
                  .entryPoint = "NGIN_Bootstrap_Samples_Missing",
                  .autoApply = true,
              },
          .contents = {},
          .modules = {},
          .plugins = {},
      });

  auto app = builder->Build();
  REQUIRE_FALSE(app.HasValue());
  REQUIRE(app.Error().code == NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("ApplicationBuilderSkipsOptionalAutoAppliedPackageWithWarning",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.OptionalWarning");
  builder->SetConfiguration("Builder.OptionalWarning.Target");
  builder->Packages()
      .Add({
          .name = "Samples.OptionalPackage",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = true,
      })
      .AddManifest(NGIN::Core::PackageManifest{
          .schemaVersion = 1,
          .name = "Samples.OptionalPackage",
          .version = "0.1.0",
          .compatiblePlatformRange = ">=0.1.0 <1.0.0",
          .operatingSystems = {"linux", "windows", "macos"},
          .dependencies = {},
          .bootstrap =
              NGIN::Core::PackageBootstrapDescriptor{
                  .mode = NGIN::Core::PackageBootstrapMode::BuilderHookV1,
                  .entryPoint = "NGIN_Bootstrap_Samples_OptionalMissing",
                  .autoApply = true,
              },
          .contents = {},
          .modules = {},
          .plugins = {},
      });

  auto app = builder->Build();
  REQUIRE(app.HasValue());

  auto report = app.Value()->GetStartupReport();
  REQUIRE_FALSE(report.warnings.empty());
  REQUIRE(ContainsWarningMessage(
      report.warnings,
      "package bootstrap skipped for 'Samples.OptionalPackage': manifest entry "
      "point 'NGIN_Bootstrap_Samples_OptionalMissing' was not registered"));
}

TEST_CASE("ApplicationBuilderFailsOnDuplicatePackageBootstrapEntry",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.DuplicateBootstrap");
  builder->SetConfiguration("Builder.DuplicateBootstrap.Target");
  builder->Packages()
      .Add({
          .name = "Samples.Package",
          .versionRange = ">=0.1.0 <1.0.0",
          .optional = false,
      })
      .AddManifest(NGIN::Core::PackageManifest{
          .schemaVersion = 1,
          .name = "Samples.Package",
          .version = "0.1.0",
          .compatiblePlatformRange = ">=0.1.0 <1.0.0",
          .operatingSystems = {"linux", "windows", "macos"},
          .dependencies = {},
          .bootstrap =
              NGIN::Core::PackageBootstrapDescriptor{
                  .mode = NGIN::Core::PackageBootstrapMode::BuilderHookV1,
                  .entryPoint = "NGIN_Bootstrap_Samples_Package",
                  .autoApply = true,
              },
          .contents = {},
          .modules = {},
          .plugins = {},
      })
      .RegisterLinkedRegistrar(
          &NGIN_RegisterPackage_Samples_PackageSingleNoAbort)
      .RegisterLinkedRegistrar(
          &NGIN_RegisterPackage_Samples_PackageSingleNoAbort);

  auto app = builder->Build();
  REQUIRE_FALSE(app.HasValue());
  REQUIRE(app.Error().code == NGIN::Core::KernelErrorCode::AlreadyExists);
}
