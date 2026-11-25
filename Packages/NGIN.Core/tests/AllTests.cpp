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
#include <memory>
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
  REQUIRE(tempDir.has_value());
  return tempDir.value();
}

void RemovePath(const NGIN::IO::Path &path) {
  NGIN::IO::LocalFileSystem fs;
  NGIN::IO::RemoveOptions options{};
  options.recursive = true;
  options.ignoreMissing = true;
  REQUIRE(fs.RemoveAll(path, options).has_value());
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
    if (resolved.has_value()) {
      dependency = resolved.value();
    }
  }

  NGIN::Memory::Shared<NGIN::Core::IServiceProvider> services{};
  NGIN::Memory::Shared<ProviderDependencyService> dependency{};
};

struct TypedServiceConfig {
  std::string message{"typed dependency"};
};

class ITypedMessageService {
public:
  virtual ~ITypedMessageService() = default;
  [[nodiscard]] virtual auto Message() const -> std::string = 0;
};

class TypedMessageService final : public ITypedMessageService {
public:
  using Dependencies = NGIN::Core::ServiceDependencies<TypedServiceConfig>;

  explicit TypedMessageService(
      NGIN::Memory::Shared<TypedServiceConfig> config)
      : m_config(std::move(config)) {}

  [[nodiscard]] auto Message() const -> std::string override {
    return m_config->message;
  }

private:
  NGIN::Memory::Shared<TypedServiceConfig> m_config{};
};

struct TypedServiceConsumer {
  using Dependencies = NGIN::Core::ServiceDependencies<ITypedMessageService>;

  explicit TypedServiceConsumer(
      NGIN::Memory::Shared<ITypedMessageService> service)
      : message(service->Message()) {}

  std::string message{};
};

struct OrderedDependencyA {
  inline static std::vector<std::string> order{};
  OrderedDependencyA() { order.push_back("A"); }
};

struct OrderedDependencyB {
  OrderedDependencyB() { OrderedDependencyA::order.push_back("B"); }
};

struct OrderedConsumer {
  using Dependencies =
      NGIN::Core::ServiceDependencies<OrderedDependencyA, OrderedDependencyB>;

  OrderedConsumer(NGIN::Memory::Shared<OrderedDependencyA>,
                  NGIN::Memory::Shared<OrderedDependencyB>) {
    OrderedDependencyA::order.push_back("Consumer");
  }
};

struct CycleServiceB;

struct CycleServiceA {
  using Dependencies = NGIN::Core::ServiceDependencies<CycleServiceB>;
  explicit CycleServiceA(NGIN::Memory::Shared<CycleServiceB>);
};

struct CycleServiceB {
  using Dependencies = NGIN::Core::ServiceDependencies<CycleServiceA>;
  explicit CycleServiceB(NGIN::Memory::Shared<CycleServiceA>) {}
};

inline CycleServiceA::CycleServiceA(NGIN::Memory::Shared<CycleServiceB>) {}

struct ScopedDependency {
};

struct SingletonCaptiveDependency {
  using Dependencies = NGIN::Core::ServiceDependencies<ScopedDependency>;
  explicit SingletonCaptiveDependency(
      NGIN::Memory::Shared<ScopedDependency>) {}
};

#if defined(NGIN_CORE_FEATURE_REFLECTION)
struct ReflectedDependency {
  std::string value{"reflected dependency"};
};

struct MissingReflectedDependency {
};

class IReflectedService {
public:
  virtual ~IReflectedService() = default;
  [[nodiscard]] virtual auto Value() const -> std::string = 0;
};

class ReflectedService final : public IReflectedService {
public:
  ReflectedService(NGIN::Memory::Shared<ReflectedDependency> dependency,
                   NGIN::Memory::Shared<MissingReflectedDependency> optional)
      : m_dependency(std::move(dependency)), m_optional(std::move(optional)) {}

  [[nodiscard]] auto Value() const -> std::string override {
    return m_dependency->value + (m_optional ? " present" : " optional missing");
  }

private:
  NGIN::Memory::Shared<ReflectedDependency> m_dependency{};
  NGIN::Memory::Shared<MissingReflectedDependency> m_optional{};
};

inline void NginReflect(NGIN::Reflection::Tag<ReflectedService>,
                        NGIN::Reflection::TypeBuilder<ReflectedService> &builder) {
  builder.InjectableConstructor<
      NGIN::Reflection::NamedConstructorDependency<
          NGIN::Memory::Shared<ReflectedDependency>, "primary">,
      NGIN::Reflection::OptionalConstructorDependency<
          NGIN::Memory::Shared<MissingReflectedDependency>>>();
}

struct MissingReflectedConsumer {
  explicit MissingReflectedConsumer(
      NGIN::Memory::Shared<MissingReflectedDependency>) {}
};

struct UnreflectedConsumer {
  explicit UnreflectedConsumer(
      NGIN::Memory::Shared<MissingReflectedDependency>) {}
};

inline void NginReflect(
    NGIN::Reflection::Tag<MissingReflectedConsumer>,
    NGIN::Reflection::TypeBuilder<MissingReflectedConsumer> &builder) {
  builder.InjectableConstructor<
      NGIN::Memory::Shared<MissingReflectedDependency>>();
}

struct AmbiguousReflectedConsumer {
  explicit AmbiguousReflectedConsumer(
      NGIN::Memory::Shared<ReflectedDependency>) {}
  explicit AmbiguousReflectedConsumer(
      NGIN::Memory::Shared<MissingReflectedDependency>) {}
};

inline void NginReflect(
    NGIN::Reflection::Tag<AmbiguousReflectedConsumer>,
    NGIN::Reflection::TypeBuilder<AmbiguousReflectedConsumer> &builder) {
  builder
      .InjectableConstructor<NGIN::Memory::Shared<ReflectedDependency>>()
      .InjectableConstructor<
          NGIN::Memory::Shared<MissingReflectedDependency>>();
}
#endif

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
  REQUIRE(parsed.has_value());
  return parsed.value();
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
    REQUIRE(fs.CreateDirectories(parent, options).has_value());
  }
  REQUIRE(NGIN::IO::WriteAllText(fs, path, text).has_value());
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

struct ModuleTaskProbe {
  std::promise<void> entered;
  std::promise<void> release;
  std::promise<void> moduleDestroyed;
  std::shared_future<void> released{release.get_future().share()};
  NGIN::Core::ServiceScopeId scope;
  NGIN::Core::ITaskRuntime *tasks{nullptr};
  std::function<void()> onShutdown;
  bool failStart{false};
  int stops{0};
  int shutdowns{0};
  bool stopRejectedSubmission{false};
  bool shutdownSawScope{false};
  std::atomic<bool> taskSawScope{false};
  std::atomic<bool> taskRejectedSubmission{false};
};

class ModuleTaskProbeModule final : public NGIN::Core::IModule {
public:
  explicit ModuleTaskProbeModule(std::shared_ptr<ModuleTaskProbe> probe)
      : m_probe(std::move(probe)) {}

  ~ModuleTaskProbeModule() override { m_probe->moduleDestroyed.set_value(); }

  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    using namespace std::chrono_literals;
    m_probe->scope = context.ModuleScope();
    m_probe->tasks = &context.Tasks();
    auto callback = [probe = m_probe, services = &context.Services()]() {
      probe->entered.set_value();
      probe->released.wait();
      probe->taskSawScope = services->GetScopeInfo(probe->scope).has_value();
      const auto next = probe->tasks->ScheduleAfter(
          NGIN::Core::TaskLane::IO, 1ms, []() {});
      probe->taskRejectedSubmission =
          !next && next.error().code == NGIN::Core::KernelErrorCode::InvalidState;
    };
    if (m_probe->failStart) {
      auto posted = context.Tasks().ScheduleAfter(
          NGIN::Core::TaskLane::Worker, 25ms, std::move(callback));
      if (posted) {
        return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
            NGIN::Core::MakeKernelError(NGIN::Core::KernelErrorCode::InvalidState,
                                       "Test", {}, "forced start failure"));
      }
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(posted.error());
    }

    auto posted =
        context.Tasks().Submit(NGIN::Core::TaskLane::Worker, std::move(callback));
    if (!posted) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(posted.error());
    }
    return {};
  }

  auto OnStop(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    ++m_probe->stops;
    const auto posted =
        context.Tasks().Submit(NGIN::Core::TaskLane::Worker, []() {});
    m_probe->stopRejectedSubmission =
        !posted && posted.error().code == NGIN::Core::KernelErrorCode::InvalidState;
    return {};
  }

  auto OnShutdown(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    ++m_probe->shutdowns;
    m_probe->shutdownSawScope =
        context.Services().GetScopeInfo(context.ModuleScope()).has_value();
    if (m_probe->onShutdown) {
      m_probe->onShutdown();
    }
    return {};
  }

private:
  std::shared_ptr<ModuleTaskProbe> m_probe;
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

class BuilderFactoryModule final : public NGIN::Core::IModule {
public:
  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    return context.RegisterSingletonValue<bool>("Builder.Factory.Ready", true);
  }
};

struct ModuleContextObservation {
  std::string stage{};
  std::string moduleName{};
  std::string moduleRoot{};
  std::string descriptorPath{};
  std::string libraryPath{};
  std::string pluginName{};
  bool dynamic{false};
};

class ModuleContextProbeModule final : public NGIN::Core::IModule {
public:
  explicit ModuleContextProbeModule(
      std::vector<ModuleContextObservation> *observations)
      : m_observations(observations) {}

  auto OnRegister(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Capture("register", context);
    return {};
  }

  auto OnInit(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Capture("init", context);
    return {};
  }

  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Capture("start", context);
    return {};
  }

  auto OnStop(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Capture("stop", context);
    return {};
  }

  auto OnShutdown(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    Capture("shutdown", context);
    return {};
  }

private:
  void Capture(const std::string_view stage,
               const NGIN::Core::ModuleContext &context) {
    if (m_observations == nullptr) {
      return;
    }

    m_observations->push_back(ModuleContextObservation{
        .stage = std::string(stage),
        .moduleName = std::string(context.Descriptor().name),
        .moduleRoot = std::string(context.ModuleRoot()),
        .descriptorPath = std::string(context.DescriptorPath()),
        .libraryPath = std::string(context.LibraryPath()),
        .pluginName = std::string(context.PluginName()),
        .dynamic = context.IsDynamicModule(),
    });
  }

  std::vector<ModuleContextObservation> *m_observations{nullptr};
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
            sub.error());
      }
      m_tokens.push_back(sub.value());
      return {};
    };

    auto result = subscribe.operator()<NGIN::Core::ModuleLoadedEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.error());
    }

    result = subscribe.operator()<NGIN::Core::ModuleStartedEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.error());
    }

    result = subscribe.operator()<NGIN::Core::KernelRunningEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.error());
    }

    result = subscribe.operator()<NGIN::Core::KernelStoppingEvent>();
    if (!result) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          result.error());
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
            sub.error());
      }
      m_tokens.push_back(sub.value());
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

auto NGIN_Bootstrap_Samples_Package(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackage(context);
}

auto NGIN_Bootstrap_Samples_PackageAlt(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackageAlt(context);
}

auto NGIN_Bootstrap_Samples_PackageA(NGIN::Core::PackageBootstrapContext &context)
    -> NGIN::Core::CoreResult<void> {
  return BootstrapSamplesPackageA(context);
}

auto NGIN_Bootstrap_Samples_PackageB(NGIN::Core::PackageBootstrapContext &context)
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
          .has_value());
  REQUIRE(
      RegisterModule(
          catalog, b,
          [&]() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "B", &order, &orderLock);
          })
          .has_value());

  auto kernelResult = NGIN::Core::CreateKernel(MakeHostConfig(catalog));
  REQUIRE(kernelResult.has_value());
  auto kernel = kernelResult.value();

  REQUIRE(kernel->Start().has_value());
  REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Running);

  const auto report = kernel->GetStartupReport();
  REQUIRE(report.resolvedModules.size() == 2);
  REQUIRE(report.failures.empty());

  REQUIRE(kernel->Shutdown().has_value());
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
          .has_value());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.has_value());
  REQUIRE(start.error().code == NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("ModuleCapabilityExclusivityIsValidatedBeforeLifecycle",
          "[runtime][resolver][capability]") {
  SECTION("Conflicting active providers fail deterministically") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto providerB = MakeDescriptor("Core.Provider.B");
    providerB.capabilities.push_back(NGIN::Core::ModuleCapability{
        .name = "Storage.DefaultProvider",
        .exclusive = false,
    });
    auto providerA = MakeDescriptor("Core.Provider.A");
    providerA.capabilities.push_back(NGIN::Core::ModuleCapability{
        .name = "Storage.DefaultProvider",
        .exclusive = true,
    });

    NGIN::UInt32 factoryCalls = 0;
    const auto factory = [&]() -> NGIN::Core::CoreResult<
                             NGIN::Memory::Shared<NGIN::Core::IModule>> {
      ++factoryCalls;
      return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
          "provider", nullptr, nullptr);
    };

    REQUIRE(RegisterModule(catalog, providerB, factory).has_value());
    REQUIRE(RegisterModule(catalog, providerA, factory).has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
            NGIN::Core::KernelErrorCode::CapabilityConflict);
    REQUIRE(start.error().message ==
            "exclusive capability 'Storage.DefaultProvider' has multiple "
            "active providers: Core.Provider.A, Core.Provider.B");
    REQUIRE(factoryCalls == 0);
  }

  SECTION("Non-exclusive providers can coexist") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto providerA = MakeDescriptor("Core.Diagnostics.A");
    providerA.capabilities.push_back(NGIN::Core::ModuleCapability{
        .name = "Diagnostics.Provider",
        .exclusive = false,
    });
    auto providerB = MakeDescriptor("Core.Diagnostics.B");
    providerB.capabilities = providerA.capabilities;

    REQUIRE(RegisterModule(
                catalog, providerA,
                []() -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<NGIN::Core::IModule>> {
                  return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                    HookModule>(
                      "diagnostics-a", nullptr, nullptr);
                })
                .has_value());
    REQUIRE(RegisterModule(
                catalog, providerB,
                []() -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<NGIN::Core::IModule>> {
                  return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                    HookModule>(
                      "diagnostics-b", nullptr, nullptr);
                })
                .has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    REQUIRE(kernel->Start().has_value());
    REQUIRE(kernel->GetStartupReport().resolvedModules.size() == 2);
    REQUIRE(kernel->Shutdown().has_value());
  }

  SECTION("Explicit selection activates one exclusive provider") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));

    auto providerA = MakeDescriptor("Core.Storage.Memory");
    providerA.capabilities.push_back(NGIN::Core::ModuleCapability{
        .name = "Storage.DefaultProvider",
        .exclusive = true,
    });
    auto providerB = MakeDescriptor("Core.Storage.File");
    providerB.capabilities = providerA.capabilities;

    REQUIRE(RegisterModule(
                catalog, providerA,
                []() -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<NGIN::Core::IModule>> {
                  return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                    HookModule>(
                      "memory", nullptr, nullptr);
                })
                .has_value());
    REQUIRE(RegisterModule(
                catalog, providerB,
                []() -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<NGIN::Core::IModule>> {
                  return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                    HookModule>(
                      "file", nullptr, nullptr);
                })
                .has_value());

    auto config = MakeHostConfig(catalog);
    config.requestedModules = {"Core.Storage.File"};
    auto kernel = NGIN::Core::CreateKernel(config).value();
    REQUIRE(kernel->Start().has_value());
    REQUIRE(kernel->GetStartupReport().resolvedModules ==
            std::vector<std::string>{"Core.Storage.File"});
    REQUIRE(kernel->Shutdown().has_value());
  }
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
            .has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
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
            .has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
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
            .has_value());
    REQUIRE(
        RegisterModule(catalog, user,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "User", nullptr, nullptr);
                       })
            .has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
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
            .has_value());
    REQUIRE(
        RegisterModule(catalog, app,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "AppGame", nullptr, nullptr);
                       })
            .has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
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
            .has_value());
    REQUIRE(
        RegisterModule(catalog, late,
                       []() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
                         return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                           HookModule>(
                             "Late", nullptr, nullptr);
                       })
            .has_value());

    auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
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
          .has_value());

  REQUIRE(
      RegisterModule(
          catalog, b,
          [&]() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule, HookModule>(
                "B", &order, &orderLock, HookModule::FailStage::Init);
          })
          .has_value());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.has_value());
  REQUIRE(start.error().code ==
          NGIN::Core::KernelErrorCode::ModuleLifecycleFailure);
  REQUIRE(start.error().cause != nullptr);

  REQUIRE(std::find(order.begin(), order.end(), "start:A") == order.end());
  REQUIRE(std::find(order.begin(), order.end(), "shutdown:A") != order.end());
}

TEST_CASE("ServiceRegistrySupportsSingletonScopedTransient",
          "[runtime][services]") {
  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(static_cast<bool>(registry));

  auto scopeA = registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "A");
  auto scopeB = registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "B");
  REQUIRE(scopeA.has_value());
  REQUIRE(scopeB.has_value());

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
              .has_value());

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
                      .ownerScope = scopeA.value(),
                  })
              .has_value());

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
                      .ownerScope = scopeA.value(),
                  })
              .has_value());

  auto s1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Singleton");
  auto s2 = registry->ResolveRequired<NGIN::UInt32>("Svc.Singleton");
  REQUIRE(s1.has_value());
  REQUIRE(s2.has_value());
  REQUIRE(s1.value().Get() == s2.value().Get());
  REQUIRE(*s1.value() == 1);
  REQUIRE(*s2.value() == 1);

  auto scopedA1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Scoped", scopeA.value());
  auto scopedA2 = registry->ResolveRequired<NGIN::UInt32>("Svc.Scoped", scopeA.value());
  auto scopedB1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Scoped", scopeB.value());
  REQUIRE(scopedA1.has_value());
  REQUIRE(scopedA2.has_value());
  REQUIRE(scopedB1.has_value());
  REQUIRE(scopedA1.value().Get() == scopedA2.value().Get());
  REQUIRE(scopedB1.value().Get() != scopedA1.value().Get());

  auto t1 = registry->ResolveRequired<NGIN::UInt32>("Svc.Transient", scopeA.value());
  auto t2 = registry->ResolveRequired<NGIN::UInt32>("Svc.Transient", scopeA.value());
  REQUIRE(t1.has_value());
  REQUIRE(t2.has_value());
  REQUIRE(t1.value().Get() != t2.value().Get());

  REQUIRE(registry->EndScope(scopeA.value()).has_value());
  auto scopedAfterEnd = registry->ResolveOptional<NGIN::UInt32>("Svc.Scoped", scopeA.value());
  REQUIRE(scopedAfterEnd.has_value());
  REQUIRE_FALSE(scopedAfterEnd.value().has_value());
}

TEST_CASE("ServiceRegistryAutoConstructsLazyServices",
          "[runtime][services]") {
  AutoConstructedService::constructed = 0;

  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(static_cast<bool>(registry));

  REQUIRE(registry->RegisterSingleton<AutoConstructedService>().has_value());
  REQUIRE(AutoConstructedService::constructed == 0);

  auto first = registry->ResolveRequired<AutoConstructedService>();
  auto second = registry->ResolveRequired<AutoConstructedService>();
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(AutoConstructedService::constructed == 1);
  REQUIRE(first.value().Get() == second.value().Get());

  auto scopeA =
      registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "Auto.A");
  auto scopeB =
      registry->BeginScope(NGIN::Core::ServiceScopeKind::Module, "Auto.B");
  REQUIRE(scopeA.has_value());
  REQUIRE(scopeB.has_value());

  REQUIRE(registry
              ->RegisterScoped<ProviderDependencyService>(
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Scoped,
                      .ownerScope = scopeA.value(),
                  })
              .has_value());
  REQUIRE(registry
              ->RegisterTransient<ProviderConsumerService>(
                  NGIN::Core::ServiceRegistrationOptions{
                      .lifetime = NGIN::Core::ServiceLifetime::Transient,
                      .ownerScope = scopeA.value(),
                  })
              .has_value());

  ProviderDependencyService::constructed = 0;
  ProviderConsumerService::constructed = 0;

  auto consumerA1 =
      registry->ResolveRequired<ProviderConsumerService>(scopeA.value());
  auto consumerA2 =
      registry->ResolveRequired<ProviderConsumerService>(scopeA.value());
  REQUIRE(consumerA1.has_value());
  REQUIRE(consumerA2.has_value());
  REQUIRE(consumerA1.value().Get() != consumerA2.value().Get());
  REQUIRE(ProviderConsumerService::constructed == 2);
  REQUIRE(ProviderDependencyService::constructed == 1);
  REQUIRE(consumerA1.value()->dependency.Get() ==
          consumerA2.value()->dependency.Get());
  REQUIRE(consumerA1.value()->dependency->value == 7);

  auto consumerB =
      registry->ResolveRequired<ProviderConsumerService>(scopeB.value());
  REQUIRE(consumerB.has_value());
  REQUIRE(ProviderDependencyService::constructed == 2);
  REQUIRE(consumerB.value()->dependency.Get() !=
          consumerA1.value()->dependency.Get());
}

TEST_CASE("ServiceRegistryReportsTypedResolutionErrors",
          "[runtime][services]") {
  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(static_cast<bool>(registry));

  REQUIRE(registry
              ->RegisterSingletonValue<std::string>("Svc.Message", "hello")
              .has_value());

  auto wrongType = registry->ResolveRequired<NGIN::UInt32>("Svc.Message");
  REQUIRE_FALSE(wrongType.has_value());
  REQUIRE(wrongType.error().code ==
          NGIN::Core::KernelErrorCode::InvalidArgument);

  auto missingOptional = registry->ResolveOptional<std::string>("Svc.Missing");
  REQUIRE(missingOptional.has_value());
  REQUIRE_FALSE(missingOptional.value().has_value());

  auto missingRequired = registry->ResolveRequired<std::string>("Svc.Missing");
  REQUIRE_FALSE(missingRequired.has_value());
  REQUIRE(missingRequired.error().code ==
          NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("ServiceRegistryConstructsDeclaredTypedDependencies",
          "[runtime][services][di]") {
  auto registry = NGIN::Core::CreateServiceRegistry();

  REQUIRE(registry->RegisterSingleton<TypedServiceConfig>().has_value());
  REQUIRE(
      (registry
           ->RegisterSingleton<ITypedMessageService, TypedMessageService>()
           .has_value()));
  REQUIRE(registry->RegisterSingleton<TypedServiceConsumer>().has_value());

  auto consumer = registry->ResolveRequired<TypedServiceConsumer>();
  REQUIRE(consumer.has_value());
  REQUIRE(consumer.value()->message == "typed dependency");

  OrderedDependencyA::order.clear();
  REQUIRE(registry->RegisterSingleton<OrderedDependencyA>().has_value());
  REQUIRE(registry->RegisterSingleton<OrderedDependencyB>().has_value());
  REQUIRE(registry->RegisterSingleton<OrderedConsumer>().has_value());
  REQUIRE(registry->ResolveRequired<OrderedConsumer>().has_value());
  REQUIRE(OrderedDependencyA::order ==
          std::vector<std::string>{"A", "B", "Consumer"});

  const auto diagnostics = registry->Diagnostics();
  const auto consumerEntry = std::find_if(
      diagnostics.registrations.begin(), diagnostics.registrations.end(),
      [](const NGIN::Core::ServiceRegistrationDiagnostics &entry) {
        return entry.key.typeId ==
               NGIN::Core::TypeServiceKey<TypedServiceConsumer>().typeId;
      });
  REQUIRE(consumerEntry != diagnostics.registrations.end());
  REQUIRE(consumerEntry->lifetime == NGIN::Core::ServiceLifetime::Singleton);
  REQUIRE(consumerEntry->dependencies.size() == 1);
  REQUIRE(consumerEntry->activationCount == 1);
  REQUIRE(consumerEntry->failureCount == 0);
  REQUIRE(consumerEntry->cachedInstanceCount == 1);
}

TEST_CASE("ServiceRegistrySupportsNamedInterfaceMappingsAndRejectsDuplicates",
          "[runtime][services][di]") {
  auto registry = NGIN::Core::CreateServiceRegistry();
  REQUIRE(registry->RegisterSingleton<TypedServiceConfig>().has_value());
  REQUIRE((registry
               ->RegisterSingleton<ITypedMessageService, TypedMessageService>(
                   "Message.Primary")
               .has_value()));

  auto named = registry->ResolveRequired<ITypedMessageService>("Message.Primary");
  REQUIRE(named.has_value());
  REQUIRE(named.value()->Message() == "typed dependency");

  auto duplicate =
      registry->RegisterSingleton<ITypedMessageService, TypedMessageService>(
          "Message.Primary");
  REQUIRE_FALSE(duplicate.has_value());
  REQUIRE(duplicate.error().code ==
          NGIN::Core::KernelErrorCode::AlreadyExists);

  auto ambiguousContract = registry->RegisterSingleton<std::string>(
      "Message.Primary", NGIN::Core::ServiceRegistrationOptions{});
  REQUIRE_FALSE(ambiguousContract.has_value());
  REQUIRE(ambiguousContract.error().code ==
          NGIN::Core::KernelErrorCode::AlreadyExists);
}

TEST_CASE("ServiceRegistryReportsDependencyCyclesAndInvalidLifetimeCapture",
          "[runtime][services][di]") {
  SECTION("Cycle includes the complete dependency path") {
    auto registry = NGIN::Core::CreateServiceRegistry();
    REQUIRE(registry->RegisterSingleton<CycleServiceA>().has_value());
    REQUIRE(registry->RegisterSingleton<CycleServiceB>().has_value());

    auto cycle = registry->ResolveRequired<CycleServiceA>();
    REQUIRE_FALSE(cycle.has_value());
    REQUIRE(cycle.error().code ==
            NGIN::Core::KernelErrorCode::DependencyCycle);
    REQUIRE(cycle.error().dependencyPath.find("CycleServiceA") !=
            std::string::npos);
    REQUIRE(cycle.error().dependencyPath.find("CycleServiceB") !=
            std::string::npos);
  }

  SECTION("Missing dependency includes the requested chain") {
    auto registry = NGIN::Core::CreateServiceRegistry();
    REQUIRE(registry->RegisterSingleton<TypedServiceConsumer>().has_value());

    auto missing = registry->ResolveRequired<TypedServiceConsumer>();
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().code == NGIN::Core::KernelErrorCode::NotFound);
    REQUIRE(missing.error().dependencyPath.find("TypedServiceConsumer") !=
            std::string::npos);
    REQUIRE(missing.error().dependencyPath.find("ITypedMessageService") !=
            std::string::npos);
  }

  SECTION("Singleton cannot capture scoped dependency") {
    auto registry = NGIN::Core::CreateServiceRegistry();
    auto scope = registry->BeginScope(NGIN::Core::ServiceScopeKind::Operation,
                                      "Lifetime.Test");
    REQUIRE(scope.has_value());
    REQUIRE(registry
                ->RegisterScoped<ScopedDependency>(
                    NGIN::Core::ServiceRegistrationOptions{
                        .lifetime = NGIN::Core::ServiceLifetime::Scoped,
                        .ownerScope = scope.value(),
                    })
                .has_value());
    REQUIRE(registry->RegisterSingleton<SingletonCaptiveDependency>()
                .has_value());

    auto captive =
        registry->ResolveRequired<SingletonCaptiveDependency>(scope.value());
    REQUIRE_FALSE(captive.has_value());
    REQUIRE(captive.error().code ==
            NGIN::Core::KernelErrorCode::InvalidArgument);
    REQUIRE(captive.error().dependencyPath.find("ScopedDependency") !=
            std::string::npos);
  }
}

TEST_CASE("ServiceRegistrySerializesSingletonActivationAndReportsFailures",
          "[runtime][services][di]") {
  auto registry = NGIN::Core::CreateServiceRegistry();
  std::atomic<NGIN::UInt32> activations{0};
  REQUIRE(registry
              ->RegisterFactory<NGIN::UInt64>(
                  "Concurrent.Singleton",
                  [&](NGIN::Core::ServiceResolutionContext &)
                      -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<NGIN::UInt64>> {
                    ++activations;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    return NGIN::Memory::MakeShared<NGIN::UInt64>(42);
                  })
              .has_value());

  std::vector<std::future<NGIN::Core::CoreResult<
      NGIN::Memory::Shared<NGIN::UInt64>>>> resolutions;
  for (NGIN::UInt32 index = 0; index < 12; ++index) {
    resolutions.push_back(std::async(std::launch::async, [&registry] {
      return registry->ResolveRequired<NGIN::UInt64>("Concurrent.Singleton");
    }));
  }
  for (auto &resolution : resolutions) {
    auto value = resolution.get();
    REQUIRE(value.has_value());
    REQUIRE(*value.value() == 42);
  }
  REQUIRE(activations == 1);

  REQUIRE(registry
              ->RegisterFactory<std::string>(
                  "Failing.Factory",
                  [](NGIN::Core::ServiceResolutionContext &)
                      -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<std::string>> {
                    return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
                        NGIN::Core::MakeKernelError(
                            NGIN::Core::KernelErrorCode::InvalidState,
                            "Test", "Failing.Factory", "expected failure"));
                  })
              .has_value());
  auto failure = registry->ResolveRequired<std::string>("Failing.Factory");
  REQUIRE_FALSE(failure.has_value());
  REQUIRE(failure.error().message == "expected failure");

  const auto diagnostics = registry->Diagnostics();
  const auto failedEntry = std::find_if(
      diagnostics.registrations.begin(), diagnostics.registrations.end(),
      [](const NGIN::Core::ServiceRegistrationDiagnostics &entry) {
        return entry.key.ContractName() == "Failing.Factory";
      });
  REQUIRE(failedEntry != diagnostics.registrations.end());
  REQUIRE(failedEntry->activationCount == 0);
  REQUIRE(failedEntry->failureCount == 1);
}

#if defined(NGIN_CORE_FEATURE_REFLECTION)
TEST_CASE("ServiceRegistryActivatesReflectedConstructorsWithTypedBindings",
          "[runtime][services][reflection]") {
  NGIN::Reflection::ModuleRegistration module{"Core.Tests.Reflection.DI"};
  module.RegisterType<ReflectedService>();
  REQUIRE(module.Commit());

  auto registry = NGIN::Core::CreateServiceRegistry();
  auto scope = registry->BeginScope(NGIN::Core::ServiceScopeKind::Operation,
                                    "Reflection.DI");
  REQUIRE(scope.has_value());
  REQUIRE(registry
              ->RegisterSingleton<ReflectedDependency>(
                  "primary", NGIN::Core::ServiceRegistrationOptions{})
              .has_value());
  REQUIRE((registry->RegisterTransient<IReflectedService, ReflectedService>(
                        NGIN::Core::ServiceRegistrationOptions{
                            .lifetime = NGIN::Core::ServiceLifetime::Transient,
                            .ownerScope = scope.value(),
                        })
               .has_value()));

  {
    auto service =
        registry->ResolveRequired<IReflectedService>(scope.value());
    REQUIRE(service.has_value());
    CHECK(service.value()->Value() ==
          "reflected dependency optional missing");

    NGIN::Reflection::Error unloadError{};
    CHECK_FALSE(
        NGIN::Reflection::UnloadModule(module.Identity(), &unloadError));
    CHECK(unloadError.code == NGIN::Reflection::ErrorCode::Conflict);
  }

  NGIN::Reflection::ModuleRegistration generationChange{
      "Core.Tests.Reflection.Generation"};
  REQUIRE(generationChange.Commit());
  {
    auto rebuilt =
        registry->ResolveRequired<IReflectedService>(scope.value());
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt.value()->Value() ==
          "reflected dependency optional missing");
  }

  NGIN::Reflection::Error unloadError{};
  CHECK(NGIN::Reflection::UnloadModule(module.Identity(), &unloadError));
  CHECK(NGIN::Reflection::UnloadModule(generationChange.Identity(),
                                       &unloadError));
}

TEST_CASE("ServiceRegistryReportsInvalidReflectedActivationPlans",
          "[runtime][services][reflection]") {
  SECTION("Missing required reflected dependency") {
    NGIN::Reflection::ModuleRegistration module{
        "Core.Tests.Reflection.Missing"};
    module.RegisterType<MissingReflectedConsumer>();
    REQUIRE(module.Commit());

    auto registry = NGIN::Core::CreateServiceRegistry();
    REQUIRE(registry->RegisterSingleton<MissingReflectedConsumer>().has_value());
    auto missing = registry->ResolveRequired<MissingReflectedConsumer>();
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == NGIN::Core::KernelErrorCode::NotFound);
    CHECK_FALSE(missing.error().dependencyPath.empty());

    NGIN::Reflection::Error error{};
    CHECK(NGIN::Reflection::UnloadModule(module.Identity(), &error));
  }

  SECTION("Missing reflected metadata fails during registration") {
    auto registry = NGIN::Core::CreateServiceRegistry();
    auto missingMetadata = registry->RegisterSingleton<UnreflectedConsumer>();
    REQUIRE_FALSE(missingMetadata.has_value());
    CHECK(missingMetadata.error().code ==
          NGIN::Core::KernelErrorCode::ReflectionRequired);
  }

  SECTION("Ambiguous injectable constructors") {
    NGIN::Reflection::ModuleRegistration module{
        "Core.Tests.Reflection.Ambiguous"};
    module.RegisterType<AmbiguousReflectedConsumer>();
    REQUIRE(module.Commit());

    auto registry = NGIN::Core::CreateServiceRegistry();
    auto ambiguous =
        registry->RegisterSingleton<AmbiguousReflectedConsumer>();
    REQUIRE_FALSE(ambiguous.has_value());
    CHECK(ambiguous.error().code ==
          NGIN::Core::KernelErrorCode::ServiceRegistrationFailure);

    NGIN::Reflection::Error error{};
    CHECK(NGIN::Reflection::UnloadModule(module.Identity(), &error));
  }
}
#endif

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
          .has_value());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.has_value());
  REQUIRE(start.error().code ==
          NGIN::Core::KernelErrorCode::MissingRequiredDependency);
}

TEST_CASE("HostConfigInputsCliAndEnvironmentAreApplied", "[runtime][config]") {
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
          .has_value());

  const auto tempRoot = MakeTempDir("ngin-runtime-tests-config");
  const auto configPath = tempRoot.Join("base.cfg");
  WriteTextFile(configPath, "App.Value=file\nApp.Other=from_file\n");

  auto cfg = MakeHostConfig(catalog);
  cfg.workingDirectory = ToString(tempRoot);
  cfg.environmentName = "TestEnv";
  cfg.configInputs = {"base.cfg"};
  cfg.commandLineArgs = {"--App.Value=cli"};

  auto kernel = NGIN::Core::CreateKernel(cfg).value();
  REQUIRE(kernel->Start().has_value());

  auto config = kernel->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("App.Value").value() == "cli");
  REQUIRE(config->GetRaw("App.Other").value() == "from_file");
  REQUIRE(config->GetRaw("Kernel.EnvironmentName").value() == "TestEnv");
  REQUIRE(config->GetRaw("Kernel.HostName").value() == "Core.Tests");

  REQUIRE(kernel->Shutdown().has_value());
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
                "        Library=\"missing-plugin-library\"\n"
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

  auto kernel = NGIN::Core::CreateKernel(cfg).value();
  auto start = kernel->Start();
  REQUIRE_FALSE(start.has_value());
  REQUIRE(start.error().code ==
          NGIN::Core::KernelErrorCode::DynamicPluginUnsupported);
  RemovePath(root);
}

TEST_CASE("DynamicDescriptorDiscoveryCanUseInjectedFilesystem",
          "[runtime][plugin][filesystem]") {
  const auto realRoot = MakeTempDir("ngin-runtime-tests-plugin-vfs");
  const auto moduleRoot = realRoot.Join("DemoPlugin").LexicallyNormal();
  const auto descriptorPath =
      moduleRoot.Join("demo.module.xml").LexicallyNormal();
  WriteTextFile(descriptorPath,
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<Module Name=\"Core.DynamicDemo.Virtual\"\n"
                "        Library=\"virtual-plugin-library\"\n"
                "        Registrar=\"NGIN_RegisterPluginCustom\"\n"
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
                "  <Capabilities>\n"
                "    <Capability Name=\"Storage.DefaultProvider\" "
                "Exclusive=\"true\" />\n"
                "    <Capability Name=\"Diagnostics.Provider\" />\n"
                "  </Capabilities>\n"
                "</Module>\n");

  auto fileSystem = NGIN::Memory::MakeSharedAs<NGIN::IO::IFileSystem,
                                               NGIN::IO::LocalFileSystem>();
  auto pluginCatalog =
      NGIN::Memory::MakeSharedAs<NGIN::Core::IPluginCatalog,
                                 NGIN::Core::FilesystemPluginCatalog>(
          std::vector<std::string>{ToString(realRoot)}, fileSystem);

  std::vector<NGIN::Core::ModuleDescriptor> descriptors{};
  auto collect = pluginCatalog->CollectDescriptors(descriptors);
  REQUIRE(collect.has_value());
  REQUIRE(descriptors.size() == 1);
  REQUIRE(descriptors.front().name == "Core.DynamicDemo.Virtual");
  REQUIRE(descriptors.front().pluginName == "DemoPlugin");
  REQUIRE(descriptors.front().descriptorPath == ToString(descriptorPath));
  REQUIRE(descriptors.front().moduleRoot == ToString(moduleRoot));
  REQUIRE(descriptors.front().pluginLibrary.find("virtual-plugin-library") !=
          std::string::npos);
  REQUIRE(descriptors.front().pluginRegistrar == "NGIN_RegisterPluginCustom");
  REQUIRE(descriptors.front().capabilities.size() == 2);
  REQUIRE(descriptors.front().capabilities[0].name ==
          "Storage.DefaultProvider");
  REQUIRE(descriptors.front().capabilities[0].exclusive);
  REQUIRE(descriptors.front().capabilities[1].name == "Diagnostics.Provider");
  REQUIRE_FALSE(descriptors.front().capabilities[1].exclusive);
  RemovePath(realRoot);
}

#if defined(NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH)
TEST_CASE("DynamicPluginLoaderStartsRegisteredModule", "[runtime][plugin]") {
  auto catalog = NGIN::Core::CreateStaticModuleCatalog();
  REQUIRE(static_cast<bool>(catalog));

  const auto root = MakeTempDir("ngin-runtime-tests-dynamic-plugin");
  const auto moduleRoot = root.Join("DemoPlugin").LexicallyNormal();
  const auto descriptorPath =
      moduleRoot.Join("fixture.module.xml").LexicallyNormal();
  WriteTextFile(descriptorPath,
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<Module Name=\"Core.DynamicFixture\"\n"
                "        Library=\"" NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH "\"\n"
                "        Family=\"Core\"\n"
                "        Type=\"Runtime\"\n"
                "        StartupStage=\"Services\"\n"
                "        Version=\"0.1.0\"\n"
                "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\">\n"
                "  <Compatibility>\n"
                "    <OperatingSystems>\n"
                "      <OperatingSystem Name=\"linux\" />\n"
                "      <OperatingSystem Name=\"windows\" />\n"
                "      <OperatingSystem Name=\"macos\" />\n"
                "    </OperatingSystems>\n"
                "  </Compatibility>\n"
                "  <ProvidesServices>\n"
                "    <Service Name=\"Core.DynamicFixture.Ready\" />\n"
                "  </ProvidesServices>\n"
                "</Module>\n");

  auto cfg = MakeHostConfig(catalog);
  cfg.enableDynamicPlugins = true;
  cfg.pluginSearchPaths = {ToString(root)};

  auto kernel = NGIN::Core::CreateKernel(cfg).value();
  auto start = kernel->Start();
  REQUIRE(start.has_value());

  auto ready =
      kernel->GetServices()->ResolveRequired<bool>("Core.DynamicFixture.Ready");
  REQUIRE(ready.has_value());
  REQUIRE(*ready.value());

  auto resolvedModuleRoot = kernel->GetServices()->ResolveRequired<std::string>(
      "Core.DynamicFixture.ModuleRoot");
  REQUIRE(resolvedModuleRoot.has_value());
  REQUIRE(*resolvedModuleRoot.value() == ToString(moduleRoot));

  auto resolvedDescriptorPath =
      kernel->GetServices()->ResolveRequired<std::string>(
          "Core.DynamicFixture.DescriptorPath");
  REQUIRE(resolvedDescriptorPath.has_value());
  REQUIRE(*resolvedDescriptorPath.value() == ToString(descriptorPath));

  auto resolvedLibraryPath = kernel->GetServices()->ResolveRequired<std::string>(
      "Core.DynamicFixture.LibraryPath");
  REQUIRE(resolvedLibraryPath.has_value());
  REQUIRE(*resolvedLibraryPath.value() ==
          ToString(NGIN::IO::Path(NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH)
                       .LexicallyNormal()));

  auto pluginName = kernel->GetServices()->ResolveRequired<std::string>(
      "Core.DynamicFixture.PluginName");
  REQUIRE(pluginName.has_value());
  REQUIRE(*pluginName.value() == "DemoPlugin");

  auto isDynamic = kernel->GetServices()->ResolveRequired<bool>(
      "Core.DynamicFixture.IsDynamic");
  REQUIRE(isDynamic.has_value());
  REQUIRE(*isDynamic.value());
  REQUIRE(kernel->Shutdown().has_value());
  RemovePath(root);
}

TEST_CASE("DynamicPluginLoaderReportsBinaryAndFactoryFailures",
          "[runtime][plugin]") {
  SECTION("Missing registrar symbol fails") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));
    const auto root = MakeTempDir("ngin-runtime-tests-plugin-missing-symbol");
    WriteTextFile(root.Join("DemoPlugin").Join("fixture.module.xml"),
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<Module Name=\"Core.DynamicFixture\"\n"
                  "        Library=\"" NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH "\"\n"
                  "        Registrar=\"NGIN_MissingPluginRegistrar\"\n"
                  "        Family=\"Core\"\n"
                  "        Version=\"0.1.0\"\n"
                  "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\" />\n");

    auto cfg = MakeHostConfig(catalog);
    cfg.enableDynamicPlugins = true;
    cfg.pluginSearchPaths = {ToString(root)};
    auto kernel = NGIN::Core::CreateKernel(cfg).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
            NGIN::Core::KernelErrorCode::DynamicPluginUnsupported);
    RemovePath(root);
  }

  SECTION("Registrar failure is surfaced") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));
    const auto root = MakeTempDir("ngin-runtime-tests-plugin-registrar-fails");
    WriteTextFile(root.Join("DemoPlugin").Join("fixture.module.xml"),
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<Module Name=\"Core.DynamicFixture\"\n"
                  "        Library=\"" NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH "\"\n"
                  "        Registrar=\"NGIN_RegisterPluginFailing\"\n"
                  "        Family=\"Core\"\n"
                  "        Version=\"0.1.0\"\n"
                  "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\" />\n");

    auto cfg = MakeHostConfig(catalog);
    cfg.enableDynamicPlugins = true;
    cfg.pluginSearchPaths = {ToString(root)};
    auto kernel = NGIN::Core::CreateKernel(cfg).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
            NGIN::Core::KernelErrorCode::ModuleFactoryFailure);
    RemovePath(root);
  }

  SECTION("Missing factory for descriptor fails") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));
    const auto root = MakeTempDir("ngin-runtime-tests-plugin-missing-factory");
    WriteTextFile(root.Join("DemoPlugin").Join("fixture.module.xml"),
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<Module Name=\"Core.DynamicMissingFactory\"\n"
                  "        Library=\"" NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH "\"\n"
                  "        Family=\"Core\"\n"
                  "        Version=\"0.1.0\"\n"
                  "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\" />\n");

    auto cfg = MakeHostConfig(catalog);
    cfg.enableDynamicPlugins = true;
    cfg.pluginSearchPaths = {ToString(root)};
    auto kernel = NGIN::Core::CreateKernel(cfg).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
            NGIN::Core::KernelErrorCode::ModuleFactoryFailure);
    RemovePath(root);
  }

  SECTION("Duplicate static and dynamic module fails before loading") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));
    REQUIRE(RegisterModule(
                catalog, MakeDescriptor("Core.DynamicFixture"),
                []() -> NGIN::Core::CoreResult<
                          NGIN::Memory::Shared<NGIN::Core::IModule>> {
                  return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                    HookModule>(
                      "duplicate", nullptr, nullptr);
                })
                .has_value());
    const auto root = MakeTempDir("ngin-runtime-tests-plugin-duplicate");
    WriteTextFile(root.Join("DemoPlugin").Join("fixture.module.xml"),
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<Module Name=\"Core.DynamicFixture\"\n"
                  "        Library=\"" NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH "\"\n"
                  "        Family=\"Core\"\n"
                  "        Version=\"0.1.0\"\n"
                  "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\" />\n");

    auto cfg = MakeHostConfig(catalog);
    cfg.enableDynamicPlugins = true;
    cfg.pluginSearchPaths = {ToString(root)};
    auto kernel = NGIN::Core::CreateKernel(cfg).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code == NGIN::Core::KernelErrorCode::AlreadyExists);
    RemovePath(root);
  }

  SECTION("Dynamic module dependency checks run before binary load") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    REQUIRE(static_cast<bool>(catalog));
    const auto root = MakeTempDir("ngin-runtime-tests-plugin-missing-dep");
    WriteTextFile(root.Join("DemoPlugin").Join("fixture.module.xml"),
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<Module Name=\"Core.DynamicFixture\"\n"
                  "        Library=\"" NGIN_CORE_TEST_DYNAMIC_PLUGIN_PATH "\"\n"
                  "        Family=\"Core\"\n"
                  "        Version=\"0.1.0\"\n"
                  "        CompatiblePlatformRange=\">=0.1.0 &lt;1.0.0\">\n"
                  "  <Dependencies>\n"
                  "    <Dependency Name=\"Core.MissingDynamicDependency\" />\n"
                  "  </Dependencies>\n"
                  "</Module>\n");

    auto cfg = MakeHostConfig(catalog);
    cfg.enableDynamicPlugins = true;
    cfg.pluginSearchPaths = {ToString(root)};
    auto kernel = NGIN::Core::CreateKernel(cfg).value();
    auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code == NGIN::Core::KernelErrorCode::NotFound);
    RemovePath(root);
  }
}
#endif

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
              .has_value());
  REQUIRE(runtime
              ->Submit(NGIN::Core::TaskLane::Worker,
                       [&]() { hits.fetch_add(1, std::memory_order_relaxed); })
              .has_value());
  REQUIRE(runtime
              ->Submit(NGIN::Core::TaskLane::Render,
                       [&]() { hits.fetch_add(1, std::memory_order_relaxed); })
              .has_value());

  REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::IO).has_value());
  REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::Worker).has_value());
  REQUIRE(runtime->Barrier(NGIN::Core::TaskLane::Render).has_value());
  REQUIRE(hits.load(std::memory_order_relaxed) == 3);
}

TEST_CASE("TaskBarriersIncludeRunningAndDelayedCallbacks", "[runtime][tasks]") {
  using namespace std::chrono_literals;
  using NGIN::Core::TaskLane;
  auto runtime = NGIN::Core::CreateTaskRuntime(1);
  REQUIRE(runtime);

  bool allLanes = false;
  SECTION("Single lane") {}
  SECTION("All lanes") { allLanes = true; }

  std::promise<void> entered;
  std::promise<void> release;
  REQUIRE(runtime->Submit(TaskLane::Worker, [&, gate = release.get_future().share()] {
                   entered.set_value();
                   gate.wait();
                 }).has_value());
  entered.get_future().wait();

  std::promise<void> waiterEntered;
  auto barrier = std::async(std::launch::async, [&]() {
    waiterEntered.set_value();
    return allLanes ? runtime->BarrierAll() : runtime->Barrier(TaskLane::Worker);
  });
  waiterEntered.get_future().wait();
  const auto independentLane = runtime->Barrier(TaskLane::IO);
  const auto blocked = barrier.wait_for(25ms);
  const auto idle = runtime->WaitIdle(0ms);
  release.set_value();

  REQUIRE(independentLane.has_value());
  REQUIRE(blocked == std::future_status::timeout);
  REQUIRE(idle.has_value());
  REQUIRE_FALSE(idle.value());
  REQUIRE(barrier.get().has_value());

  std::atomic<bool> ranDelayed{false};
  REQUIRE(runtime->ScheduleAfter(TaskLane::Worker, 25ms,
                                 [&]() { ranDelayed = true; }).has_value());
  REQUIRE((allLanes ? runtime->BarrierAll() : runtime->Barrier(TaskLane::Worker))
              .has_value());
  REQUIRE(ranDelayed.load());
}

TEST_CASE("TasksStayActiveUntilCallbackCapturesAreDestroyed", "[runtime][tasks]") {
  using namespace std::chrono_literals;
  using NGIN::Core::TaskLane;
  auto runtime = NGIN::Core::CreateTaskRuntime(1);
  REQUIRE(runtime);
  REQUIRE_FALSE(runtime->Submit(TaskLane::Worker, {}).has_value());
  REQUIRE_FALSE(runtime->ScheduleAfter(TaskLane::Render, 1ms, []() {}).has_value());
  REQUIRE(runtime->WaitIdle(0ms).value());

  std::promise<void> releaseCallback;
  std::promise<void> releaseCapture;
  const auto destroying = std::make_shared<std::promise<void>>();
  auto destroyingFuture = destroying->get_future();
  auto capture = std::shared_ptr<void>(
      nullptr, [destroying, gate = releaseCapture.get_future().share()](void *) {
        destroying->set_value();
        gate.wait();
      });
  const auto selfWaitRejected = std::make_shared<std::atomic<bool>>(false);
  const auto submitted = runtime->Submit(
      TaskLane::Worker,
      [capture, gate = releaseCallback.get_future().share(),
       selfWaitRejected, tasks = runtime.Get()] {
        gate.wait();
        const auto wait = tasks->Barrier(TaskLane::Worker);
        *selfWaitRejected =
            !wait && wait.error().code == NGIN::Core::KernelErrorCode::InvalidState;
        (void)capture;
      });
  if (!submitted) {
    releaseCapture.set_value();
  }
  capture.reset();
  REQUIRE(submitted.has_value());
  releaseCallback.set_value();
  const auto captureStarted = destroyingFuture.wait_for(1s);
  const auto whileDestroying = runtime->WaitIdle(0ms);
  releaseCapture.set_value();

  REQUIRE(captureStarted == std::future_status::ready);
  REQUIRE(whileDestroying.has_value());
  REQUIRE_FALSE(whileDestroying.value());
  const auto idle = runtime->WaitIdle(1s);
  REQUIRE(idle.has_value());
  REQUIRE(idle.value());
  REQUIRE(selfWaitRejected->load());
}

TEST_CASE("TaskCallbackCanReleaseLastRuntimeHandle", "[runtime][tasks]") {
  using namespace std::chrono_literals;
  using NGIN::Core::TaskLane;
  auto runtime = NGIN::Core::CreateTaskRuntime(1);
  REQUIRE(runtime);
  const auto discarded = std::make_shared<std::promise<void>>();
  auto discardedFuture = discarded->get_future();
  auto capture = std::shared_ptr<void>(nullptr, [discarded](void *) {
    discarded->set_value();
  });
  REQUIRE(runtime->ScheduleAfter(TaskLane::Background, 1h, [capture]() {
                   (void)capture;
                 }).has_value());
  capture.reset();

  std::promise<void> release;
  REQUIRE(runtime->Submit(TaskLane::Worker,
                          [last = runtime, gate = release.get_future().share()]() mutable {
                            gate.wait();
                            last.Reset();
                          }).has_value());
  runtime.Reset();
  release.set_value();
  REQUIRE(discardedFuture.wait_for(1s) == std::future_status::ready);
}

TEST_CASE("KernelDrainsModuleTasksBeforeReleasingScopes", "[runtime][tasks][lifecycle]") {
  using namespace std::chrono_literals;
  using namespace NGIN::Core;
  auto catalog = CreateStaticModuleCatalog();
  auto probe = std::make_shared<ModuleTaskProbe>();
  REQUIRE(RegisterModule(catalog, MakeDescriptor("Core.TaskProbe"), [probe]() {
    return NGIN::Memory::MakeSharedAs<IModule, ModuleTaskProbeModule>(probe);
  }).has_value());
  auto config = MakeHostConfig(catalog);
  config.schedulerPolicy.workerThreads = 1;
  config.configureServices = [probe](KernelBootstrapContext &context)
      -> CoreResult<void> {
    if (!probe->failStart) {
      return {};
    }
    const auto subscribed = context.events->Subscribe<KernelStoppingEvent>(
        [probe](const TypedEventRecord<KernelStoppingEvent> &) {
          probe->release.set_value();
        },
        EventScope{.owner = "Test"});
    if (!subscribed) {
      return NGIN::Utilities::Unexpected<KernelError>(subscribed.error());
    }
    return {};
  };
  auto kernel = CreateKernel(config).value();

  SECTION("Startup failure drains a delayed callback") {
    probe->failStart = true;
    const auto start = kernel->Start();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code == KernelErrorCode::ModuleLifecycleFailure);
    REQUIRE(kernel->GetState() == KernelState::Shutdown);
    REQUIRE(probe->stops == 0);
  }

  SECTION("Timeout leaves the module scope intact and shutdown can retry") {
    const auto start = kernel->Start();
    if (!start) {
      probe->release.set_value();
    }
    REQUIRE(start.has_value());
    const auto entered = probe->entered.get_future().wait_for(1s);
    const auto moduleIdle = probe->tasks->WaitIdle(0ms);
    const auto timeout = kernel->Shutdown();
    const auto state = kernel->GetState();
    const auto scope = kernel->GetServices()->GetScopeInfo(probe->scope);
    const auto shutdowns = probe->shutdowns;
    probe->release.set_value();

    REQUIRE(entered == std::future_status::ready);
    REQUIRE(moduleIdle.has_value());
    REQUIRE_FALSE(moduleIdle.value());
    REQUIRE_FALSE(timeout.has_value());
    REQUIRE(timeout.error().code == KernelErrorCode::TaskDrainTimeout);
    REQUIRE(state == KernelState::Stopping);
    REQUIRE(scope.has_value());
    REQUIRE(shutdowns == 0);
    REQUIRE(probe->stopRejectedSubmission);
    REQUIRE_FALSE(kernel->GetTasks()->Submit(TaskLane::IO, []() {}).has_value());
    REQUIRE(kernel->Shutdown().has_value());
    REQUIRE(probe->stops == 1);
  }

  REQUIRE(probe->taskSawScope.load());
  REQUIRE(probe->taskRejectedSubmission.load());
  REQUIRE(probe->shutdowns == 1);
  REQUIRE(probe->shutdownSawScope);
  REQUIRE_FALSE(kernel->GetServices()->GetScopeInfo(probe->scope).has_value());
}

TEST_CASE("KernelCanBeReleasedByATaskCapture", "[runtime][tasks][lifecycle]") {
  using namespace std::chrono_literals;
  using namespace NGIN::Core;
  auto catalog = CreateStaticModuleCatalog();
  auto probe = std::make_shared<ModuleTaskProbe>();
  auto destroyed = probe->moduleDestroyed.get_future();
  REQUIRE(RegisterModule(catalog, MakeDescriptor("Core.TaskProbe"), [probe]() {
    return NGIN::Memory::MakeSharedAs<IModule, ModuleTaskProbeModule>(probe);
  }).has_value());
  auto config = MakeHostConfig(catalog);
  config.schedulerPolicy.workerThreads = 1;
  auto kernel = CreateKernel(config).value();
  const auto start = kernel->Start();
  if (!start) {
    probe->release.set_value();
  }
  REQUIRE(start.has_value());
  const auto entered = probe->entered.get_future().wait_for(1s);
  auto runtime = kernel->GetTasks();
  auto services = kernel->GetServices();

  std::promise<void> releaseLast;
  const auto accepted = runtime->Submit(
      TaskLane::IO, [last = kernel, gate = releaseLast.get_future().share()] {
        gate.wait();
        (void)last;
      });
  kernel.Reset();
  releaseLast.set_value();
  const auto retired = runtime->Barrier(TaskLane::IO);
  const auto scope = services->GetScopeInfo(probe->scope);
  probe->release.set_value();

  REQUIRE(entered == std::future_status::ready);
  REQUIRE(accepted.has_value());
  REQUIRE(retired.has_value());
  REQUIRE(scope.has_value());
  REQUIRE(destroyed.wait_for(1s) == std::future_status::ready);
  REQUIRE(probe->taskSawScope.load());
  REQUIRE_FALSE(services->GetScopeInfo(probe->scope).has_value());
}

TEST_CASE("ShutdownCallbacksCanReenterTheKernel", "[runtime][tasks][lifecycle]") {
  using namespace std::chrono_literals;
  using namespace NGIN::Core;
  auto policy = KernelApiThreadPolicy::Serialized;
  SECTION("Serialized") {}
  SECTION("Single thread") { policy = KernelApiThreadPolicy::SingleThreadOnly; }

  auto finished = std::make_shared<std::promise<bool>>();
  auto result = finished->get_future();
  std::thread worker([policy, finished]() {
    auto catalog = CreateStaticModuleCatalog();
    auto probe = std::make_shared<ModuleTaskProbe>();
    probe->release.set_value();
    const auto registered =
        RegisterModule(catalog, MakeDescriptor("Core.TaskProbe"), [probe]() {
          return NGIN::Memory::MakeSharedAs<IModule, ModuleTaskProbeModule>(probe);
        });
    auto config = MakeHostConfig(catalog);
    config.schedulerPolicy.workerThreads = 1;
    config.apiThreadPolicy = policy;
    auto kernel = CreateKernel(config).value();
    if (!registered || !kernel->Start()) {
      finished->set_value(false);
      return;
    }
    const auto checkReentry = [host = kernel.Get()]() {
      const auto tick = host->Tick();
      const auto shutdown = host->Shutdown();
      return !tick && tick.error().code == KernelErrorCode::InvalidState &&
             !shutdown && shutdown.error().code == KernelErrorCode::InvalidState;
    };
    bool hookRejected = false;
    probe->onShutdown = [&]() { hookRejected = checkReentry(); };

    int eventCount = 0;
    bool eventRejected = false;
    const auto subscribed = kernel->GetEvents()->Subscribe<KernelStoppingEvent>(
        [&](const TypedEventRecord<KernelStoppingEvent> &) {
          if (++eventCount == 1) {
            eventRejected = checkReentry();
          }
        },
        EventScope{.owner = "Test"});
    const auto shutdown = kernel->Shutdown();
    finished->set_value(subscribed.has_value() && shutdown.has_value() &&
                        eventCount == 1 && eventRejected && hookRejected);
  });
  const auto waited = result.wait_for(4s);
  if (waited == std::future_status::ready) {
    worker.join();
  } else {
    worker.detach();
  }
  REQUIRE(waited == std::future_status::ready);
  REQUIRE(result.get());
}

TEST_CASE("ModuleTaskWaitsObserveOnlyThatModule", "[runtime][tasks]") {
  using namespace std::chrono_literals;
  using namespace NGIN::Core;
  auto catalog = CreateStaticModuleCatalog();
  auto probe = std::make_shared<ModuleTaskProbe>();
  probe->release.set_value();
  REQUIRE(RegisterModule(catalog, MakeDescriptor("Core.TaskProbe"), [probe]() {
    return NGIN::Memory::MakeSharedAs<IModule, ModuleTaskProbeModule>(probe);
  }).has_value());
  auto config = MakeHostConfig(catalog);
  config.schedulerPolicy.workerThreads = 1;
  auto kernel = CreateKernel(config).value();
  REQUIRE(kernel->Start().has_value());

  std::promise<void> releaseHostTask;
  auto hostTasks = kernel->GetTasks();
  const auto submitted = hostTasks->Submit(
      TaskLane::Background, [gate = releaseHostTask.get_future().share()] {
        gate.wait();
      });
  const auto moduleIdle = probe->tasks->WaitIdle(1s);
  const auto hostIdle = hostTasks->WaitIdle(0ms);
  releaseHostTask.set_value();

  REQUIRE(submitted.has_value());
  REQUIRE(moduleIdle.has_value());
  REQUIRE(moduleIdle.value());
  REQUIRE(hostIdle.has_value());
  REQUIRE_FALSE(hostIdle.value());
  REQUIRE(kernel->Shutdown().has_value());
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
  REQUIRE(sub.has_value());

  REQUIRE(bus->Publish(TestUserEvent{.value = 7}).has_value());

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
  REQUIRE(sub.has_value());

  REQUIRE(
      bus->EnqueueTo(NGIN::Core::EventQueue::Main, TestUserEvent{.value = 1})
          .has_value());
  REQUIRE(bus->EnqueueTo(NGIN::Core::EventQueue::IO, TestUserEvent{.value = 2})
              .has_value());

  REQUIRE(bus->FlushFrom<TestUserEvent>(NGIN::Core::EventQueue::IO).has_value());
  REQUIRE(seen == std::vector<NGIN::UInt32>{2});

  REQUIRE(bus->Flush<TestUserEvent>().has_value());
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
  REQUIRE(low.has_value());

  auto high = bus->Subscribe<TestScopedEvent>(
      [&](const NGIN::Core::TypedEventRecord<TestScopedEvent> &record) {
        order.push_back(record.event.name + "-high");
      },
      NGIN::Core::EventScope{.owner = "scope.high"}, 5);
  REQUIRE(high.has_value());

  REQUIRE(bus->Publish(TestScopedEvent{.name = "first"}).has_value());
  REQUIRE(order == std::vector<std::string>{"first-high", "first"});

  REQUIRE(bus->Unsubscribe(high.value()).has_value());
  order.clear();
  REQUIRE(bus->Publish(TestScopedEvent{.name = "second"}).has_value());
  REQUIRE(order == std::vector<std::string>{"second"});

  bus->ClearScope(NGIN::Core::EventScope{.owner = "scope.low"});
  order.clear();
  REQUIRE(bus->Publish(TestScopedEvent{.name = "third"}).has_value());
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
  REQUIRE(typedSub.has_value());

  REQUIRE(
      bus
          ->PublishRawImmediate(NGIN::Core::RawEventRecord{
              .channel =
                  std::string(NGIN::Core::EventChannelName<TestUserEvent>()),
              .payload = NGIN::Utilities::Any<>(TestUserEvent{.value = 42})})
          .has_value());
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
  REQUIRE(rawSub.has_value());

  REQUIRE(bus->Publish(TestUserEvent{.value = 99}).has_value());
  REQUIRE(rawValues == std::vector<NGIN::UInt32>{99});
}

TEST_CASE("TypedEventSubscribersRejectPayloadMismatch", "[runtime][events]") {
  auto bus = NGIN::Core::CreateEventBus();
  REQUIRE(static_cast<bool>(bus));

  auto sub = bus->Subscribe<TestUserEvent>(
      [](const NGIN::Core::TypedEventRecord<TestUserEvent> &) {},
      NGIN::Core::EventScope{.owner = "tests"});
  REQUIRE(sub.has_value());

  auto publish = bus->PublishRawImmediate(NGIN::Core::RawEventRecord{
      .channel = std::string(NGIN::Core::EventChannelName<TestUserEvent>()),
      .payload = NGIN::Utilities::Any<>(std::string("wrong-type"))});
  REQUIRE_FALSE(publish.has_value());
  REQUIRE(publish.error().code ==
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
  REQUIRE(sub.has_value());

  REQUIRE(bus
              ->EnqueueRaw(NGIN::Core::RawEventRecord{
                  .channel = std::string(
                      NGIN::Core::EventChannelName<TestUserEvent>()),
                  .payload = NGIN::Utilities::Any<>(TestUserEvent{.value = 1}),
                  .sequence = 77})
              .has_value());

  REQUIRE(bus->FlushRawFrom(NGIN::Core::EventQueue::Main).has_value());
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
          .has_value());

  auto rawDesc = MakeDescriptor("Core.EventProbe.Raw");
  REQUIRE(
      RegisterModule(catalog, rawDesc,
                     [&]() -> NGIN::Core::CoreResult<
                               NGIN::Memory::Shared<NGIN::Core::IModule>> {
                       return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                         RawEventProbeModule>(
                           &rawEvents, &rawEventsLock);
                     })
          .has_value());

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
  REQUIRE(kernel->Start().has_value());
  REQUIRE(kernel->Shutdown().has_value());

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

  auto kernel = NGIN::Core::CreateKernel(MakeHostConfig(catalog)).value();
  REQUIRE(kernel->Start().has_value());

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
  REQUIRE(typedSub.has_value());

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
  REQUIRE(rawSub.has_value());

  REQUIRE(config
              ->SetValue(NGIN::Core::ConfigLayer::RuntimeMutable, "App.Mode",
                         "runtime")
              .has_value());

  REQUIRE(typedChanges == std::vector<std::string>{"App.Mode=runtime"});
  REQUIRE(rawChanges == std::vector<std::string>{"ConfigChanged"});
  REQUIRE(kernel->Shutdown().has_value());
}

TEST_CASE("ThreadPoliciesAreEnforced", "[runtime][threading]") {
  SECTION("SingleThreadOnly rejects non-owner thread") {
    auto catalog = NGIN::Core::CreateStaticModuleCatalog();
    auto cfg = MakeHostConfig(catalog);
    cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::SingleThreadOnly;

    auto kernel = NGIN::Core::CreateKernel(cfg).value();

    std::promise<NGIN::Core::CoreResult<void>> promise;
    auto future = promise.get_future();
    std::thread worker([&]() mutable { promise.set_value(kernel->Start()); });
    worker.join();

    auto start = future.get();
    REQUIRE_FALSE(start.has_value());
    REQUIRE(start.error().code ==
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
            .has_value());

    auto cfg = MakeHostConfig(catalog);
    cfg.apiThreadPolicy = NGIN::Core::KernelApiThreadPolicy::Serialized;
    auto kernel = NGIN::Core::CreateKernel(cfg).value();

    std::promise<NGIN::Core::CoreResult<void>> promise;
    auto future = promise.get_future();
    std::thread runner([&]() mutable { promise.set_value(kernel->Run()); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    kernel->RequestStop("test");

    runner.join();
    auto result = future.get();
    REQUIRE(result.has_value());
    REQUIRE(kernel->GetState() == NGIN::Core::KernelState::Shutdown);
  }
}

TEST_CASE("ApplicationBuilderRegistersStaticModuleWithSimpleApi",
          "[builder][host]") {
  class SimpleBuilderModule final : public NGIN::Core::IModule {
  public:
    auto OnStart(NGIN::Core::ModuleContext &context) noexcept
        -> NGIN::Core::CoreResult<void> override {
      return context.RegisterSingletonValue<bool>("Builder.Simple.Ready", true);
    }
  };

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.Simple")
      .AddDefaultServices()
      .AddConfiguration()
      .AddModule<SimpleBuilderModule>("Builder.Simple.Module");

  auto app = builder->Build();
  REQUIRE(app.has_value());
  auto host = app.value();
  REQUIRE(host->Start().has_value());

  auto ready = host->GetServices()->ResolveRequired<bool>("Builder.Simple.Ready");
  REQUIRE(ready.has_value());
  REQUIRE(*ready.value());

  auto report = host->GetStartupReport();
  REQUIRE(ContainsString(report.resolvedModules, "Builder.Simple.Module"));
  REQUIRE(host->Shutdown().has_value());
}

TEST_CASE("ApplicationBuilderUsesInjectedHostRunLoop",
          "[builder][host][run-loop]") {
  class RecordingRunLoop final : public NGIN::Core::IHostRunLoop {
  public:
    auto Run(NGIN::Core::IApplicationHost &host) noexcept
        -> NGIN::Core::CoreResult<void> override {
      ++runCount;
      auto started = host.Start();
      if (!started) {
        return started;
      }
      startedHost = true;

      auto ticked = host.Tick();
      if (!ticked) {
        return ticked;
      }
      tickedHost = true;

      host.RequestStop("recording run loop complete");
      observedStop = host.IsStopRequested();
      return host.Shutdown();
    }

    void Wake() noexcept override { ++wakeCount; }

    int runCount{0};
    int wakeCount{0};
    bool startedHost{false};
    bool tickedHost{false};
    bool observedStop{false};
  };

  auto runLoop = std::make_shared<RecordingRunLoop>();
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.RunLoop").UseRunLoop(runLoop);

  auto app = builder->Build();
  REQUIRE(app.has_value());
  REQUIRE(app.value()->Run().has_value());
  REQUIRE(runLoop->runCount == 1);
  REQUIRE(runLoop->wakeCount == 1);
  REQUIRE(runLoop->startedHost);
  REQUIRE(runLoop->tickedHost);
  REQUIRE(runLoop->observedStop);
}

TEST_CASE("StaticModuleContextExposesConfiguredModuleOrigin",
          "[builder][host][module]") {
  const auto moduleRoot =
      MakeTempDir("ngin-runtime-tests-static-module-root").LexicallyNormal();
  std::vector<ModuleContextObservation> observations{};

  NGIN::Core::ModuleOptions options{};
  options.moduleRoot = ToString(moduleRoot);

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.ModuleOrigin")
      .AddModule(
          "Builder.ModuleOrigin.Probe", options,
          [&observations]() -> NGIN::Core::CoreResult<
                                NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                              ModuleContextProbeModule>(
                &observations);
          });

  auto app = builder->Build();
  REQUIRE(app.has_value());
  auto host = app.value();
  REQUIRE(host->Start().has_value());
  REQUIRE(observations.size() == 3);
  REQUIRE(host->Shutdown().has_value());
  REQUIRE(observations.size() == 5);

  REQUIRE(observations[0].stage == "register");
  REQUIRE(observations[1].stage == "init");
  REQUIRE(observations[2].stage == "start");
  REQUIRE(observations[3].stage == "stop");
  REQUIRE(observations[4].stage == "shutdown");

  for (const auto &observation : observations) {
    REQUIRE(observation.moduleName == "Builder.ModuleOrigin.Probe");
    REQUIRE(observation.moduleRoot == ToString(moduleRoot));
    REQUIRE(observation.descriptorPath.empty());
    REQUIRE(observation.libraryPath.empty());
    REQUIRE(observation.pluginName.empty());
    REQUIRE_FALSE(observation.dynamic);
  }

  const auto defaultDescriptor =
      NGIN::Core::MakeModuleDescriptor("Builder.ModuleOrigin.Default");
  REQUIRE(defaultDescriptor.moduleRoot.empty());
  RemovePath(moduleRoot);
}

TEST_CASE("ApplicationBuilderSimpleApiSupportsFactoryAndConfig",
          "[builder][host]") {
  const auto tempRoot = MakeTempDir("ngin-runtime-tests-builder-simple-api");
  WriteTextFile(tempRoot.Join("app.cfg"), "Builder.Message=from_config\n");

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  NGIN::Core::ModuleOptions options{};
  options.providesServices = {"Builder.Factory.Ready"};
  builder->SetApplicationName("Builder.Factory")
      .AddConfiguration()
      .AddConfigSource(ToString(tempRoot.Join("app.cfg")))
      .AddModule(
          "Builder.Factory.Module", options,
          []() -> NGIN::Core::CoreResult<
                    NGIN::Memory::Shared<NGIN::Core::IModule>> {
            return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                              BuilderFactoryModule>();
          });

  auto app = builder->Build();
  REQUIRE(app.has_value());
  auto host = app.value();
  REQUIRE(host->Start().has_value());
  REQUIRE(host->GetConfig()->GetRaw("Builder.Message").value() ==
          "from_config");
  REQUIRE(host->GetServices()
              ->ResolveRequired<bool>("Builder.Factory.Ready")
              .has_value());
  REQUIRE(host->Shutdown().has_value());
  RemovePath(tempRoot);
}

TEST_CASE("ApplicationBuilderBuildsHostFromCode", "[builder][host]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.Tests");
  builder->SetProfile("Builder.Target");
  builder->Services()
      .AddDefaults()
      .AddConfiguration()
      .AddSingletonValue<std::string>("App.Message", "hello-builder")
      .AddSingleton<TypedServiceConfig>()
      .AddSingleton<ITypedMessageService, TypedMessageService>()
      .AddSingleton<TypedServiceConsumer>();
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
  REQUIRE(app.has_value());
  REQUIRE(app.value()->GetProfileName() == "Builder.Target");

  auto report = app.value()->GetStartupReport();
  REQUIRE(report.targetName == "Builder.Target");
  REQUIRE(report.hostName == "Builder.Tests");
  REQUIRE(report.hostType == "ConsoleApp");

  REQUIRE(app.value()->Start().has_value());

  auto services = app.value()->GetServices();
  REQUIRE(static_cast<bool>(services));

  auto resolved = services->ResolveRequired<std::string>("App.Message");
  REQUIRE(resolved.has_value());
  REQUIRE(*resolved.value() == "hello-builder");
  auto typedConsumer = services->ResolveRequired<TypedServiceConsumer>();
  REQUIRE(typedConsumer.has_value());
  REQUIRE(typedConsumer.value()->message == "typed dependency");

  auto config = app.value()->GetConfig();
  REQUIRE(static_cast<bool>(config));
  REQUIRE(config->GetRaw("Kernel.TargetName").has_value());
  REQUIRE(config->GetRaw("Kernel.TargetName").value() == "Builder.Target");

  auto secondBuild = builder->Build();
  REQUIRE_FALSE(secondBuild.has_value());
  REQUIRE(secondBuild.error().code ==
          NGIN::Core::KernelErrorCode::InvalidState);

  REQUIRE(app.value()->Shutdown().has_value());
}
TEST_CASE("ApplicationBuilderExecutesNamedPackageBootstrapEntry",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.NamedBootstrap");
  builder->SetProfile("Builder.NamedBootstrap.Target");
  builder->Packages()
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_Package)
      .ApplyBootstrap("Samples.Package", "NGIN_Bootstrap_Samples_PackageAlt");

  auto app = builder->Build();
  REQUIRE(app.has_value());
  REQUIRE(app.value()->Start().has_value());

  auto services = app.value()->GetServices();
  REQUIRE(static_cast<bool>(services));

  auto message = services->ResolveRequired<std::string>("Samples.Package.Message");
  REQUIRE(message.has_value());
  REQUIRE(*message.value() == "bootstrapped-alt");

  REQUIRE(app.value()->Shutdown().has_value());
}

TEST_CASE("ApplicationBuilderAppliesPackageBootstrapsInCodeOrder",
          "[builder][bootstrap]") {
  std::vector<std::string> order{};
  g_packageBootstrapOrder = &order;

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.AutoApply");
  builder->SetProfile("Builder.AutoApply.Target");
  builder->Packages()
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_PackageB)
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_PackageA)
      .ApplyBootstrap("Samples.PackageA")
      .ApplyBootstrap("Samples.PackageB");

  auto app = builder->Build();
  REQUIRE(app.has_value());
  REQUIRE(order ==
          std::vector<std::string>{"Samples.PackageA", "Samples.PackageB"});

  g_packageBootstrapOrder = nullptr;
}

TEST_CASE("ApplicationBuilderRejectsUnregisteredPackageBootstrap",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.RequiredFailure");
  builder->SetProfile("Builder.RequiredFailure.Target");
  builder->Packages().ApplyBootstrap("Samples.RequiredPackage");

  auto app = builder->Build();
  REQUIRE_FALSE(app.has_value());
  REQUIRE(app.error().code == NGIN::Core::KernelErrorCode::NotFound);
}

TEST_CASE("ApplicationBuilderAppliesEachBootstrapEntryOnce",
          "[builder][bootstrap]") {
  std::vector<std::string> order{};
  g_packageBootstrapOrder = &order;

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.IdempotentBootstrap");
  builder->SetProfile("Builder.IdempotentBootstrap.Target");
  builder->Packages()
      .RegisterLinkedRegistrar(&NGIN_RegisterPackage_Samples_PackageA)
      .ApplyBootstrap("Samples.PackageA")
      .ApplyBootstrap("Samples.PackageA");

  auto app = builder->Build();
  REQUIRE(app.has_value());
  REQUIRE(order == std::vector<std::string>{"Samples.PackageA"});

  g_packageBootstrapOrder = nullptr;
}

TEST_CASE("ApplicationBuilderFailsOnDuplicatePackageBootstrapEntry",
          "[builder][bootstrap]") {
  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->SetApplicationName("Builder.DuplicateBootstrap");
  builder->SetProfile("Builder.DuplicateBootstrap.Target");
  builder->Packages()
      .RegisterLinkedRegistrar(
          &NGIN_RegisterPackage_Samples_PackageSingleNoAbort)
      .RegisterLinkedRegistrar(
          &NGIN_RegisterPackage_Samples_PackageSingleNoAbort);

  auto app = builder->Build();
  REQUIRE_FALSE(app.has_value());
  REQUIRE(app.error().code == NGIN::Core::KernelErrorCode::AlreadyExists);
}
