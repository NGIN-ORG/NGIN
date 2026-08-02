#include <NGIN/UI/Hosting/Hosting.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
auto Fail(const char *message) -> int {
  std::cerr << message << '\n';
  return 1;
}

struct LifecycleMetrics final {
  int nextDependencyId{1};
  int activated{0};
  int activationCompleted{0};
  int deactivated{0};
  int cleanupCompleted{0};
  std::vector<std::string> destroyed{};
};

struct ScopedPageService final {
  explicit ScopedPageService(LifecycleMetrics &lifecycle) noexcept
      : metrics(&lifecycle), id(lifecycle.nextDependencyId++) {}
  ~ScopedPageService() {
    metrics->destroyed.push_back("service-" + std::to_string(id));
  }

  LifecycleMetrics *metrics{nullptr};
  int id{0};
};

struct HostedPageViewModel final {
  using Dependencies = NGIN::Core::ServiceDependencies<ScopedPageService>;

  explicit HostedPageViewModel(
      NGIN::Memory::Shared<ScopedPageService> pageService) noexcept
      : service(std::move(pageService)), metrics(service->metrics),
        serviceId(service->id) {}
  ~HostedPageViewModel() {
    metrics->destroyed.push_back("viewmodel-" + std::to_string(serviceId));
  }

  void Activate(NGIN::UI::ViewModelTaskScope &) noexcept {
    ++metrics->activated;
  }

  [[nodiscard]] auto ActivateAsync(NGIN::Async::TaskContext &context)
      -> NGIN::UI::ViewModelTaskScope::Task {
    co_await context.YieldNow();
    ++metrics->activationCompleted;
  }

  void Deactivate() noexcept { ++metrics->deactivated; }

  [[nodiscard]] auto DeactivateAsync(NGIN::Async::TaskContext &context)
      -> NGIN::UI::ViewModelTaskScope::Task {
    co_await context.YieldNow();
    ++metrics->cleanupCompleted;
  }

  NGIN::Memory::Shared<ScopedPageService> service{};
  LifecycleMetrics *metrics{nullptr};
  int serviceId{0};
};

struct UnregisteredViewModel final {};
struct FailingViewModel final {};

struct HostedHomePage final {};
struct HostedDetailPage final {};
struct HostedMissingPage final {};
struct HostedDetailParameter final {
  int itemId{0};
};

struct NavigationMetrics final {
  int servicesCreated{0};
  int servicesDestroyed{0};
};

struct NavigationScopedService final {
  explicit NavigationScopedService(NavigationMetrics &value) noexcept
      : metrics(&value) {
    ++metrics->servicesCreated;
  }
  ~NavigationScopedService() { ++metrics->servicesDestroyed; }
  NavigationMetrics *metrics{nullptr};
};

struct NavigationViewModel final {
  using Dependencies = NGIN::Core::ServiceDependencies<NavigationScopedService>;
  explicit NavigationViewModel(
      NGIN::Memory::Shared<NavigationScopedService> value) noexcept
      : service(std::move(value)) {}
  NGIN::Memory::Shared<NavigationScopedService> service{};
};

[[nodiscard]] auto PumpUntil(NGIN::UI::Application &application,
                             const NGIN::Utilities::Callable<bool()> &done)
    -> bool {
  for (int attempt = 0; attempt < 32; ++attempt) {
    if (done()) {
      return true;
    }
    if (!application.PumpOnce(std::chrono::milliseconds{0})) {
      return false;
    }
  }
  return done();
}
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Hosting;

  auto renderer = std::make_unique<Testing::RecordingRenderBackend>();
  auto *rendererObserver = renderer.get();
  LifecycleMetrics metrics;
  NavigationMetrics navigationMetrics;
  PageRegistry pages;

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  builder->Services()
      .AddScoped<ScopedPageService>(
          [&metrics](NGIN::Core::ServiceResolutionContext &)
              -> NGIN::Core::CoreResult<
                  NGIN::Memory::Shared<ScopedPageService>> {
            return NGIN::Memory::MakeShared<ScopedPageService>(metrics);
          })
      .AddTransient<HostedPageViewModel>()
      .AddTransient<FailingViewModel>(
          [](NGIN::Core::ServiceResolutionContext &)
              -> NGIN::Core::CoreResult<
                  NGIN::Memory::Shared<FailingViewModel>> {
            return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
                NGIN::Core::MakeKernelError(
                    NGIN::Core::KernelErrorCode::ServiceRegistrationFailure,
                    "NGIN.UI.Hosting.Tests", "FailingViewModel",
                    "ViewModel constructor failed",
                    "FailingViewModel -> TestDependency"));
          });

  auto pageBuilder = ConfigureUIPages(*builder, pages);
  pageBuilder.Services().AddScoped<NavigationScopedService>(
      [&navigationMetrics](NGIN::Core::ServiceResolutionContext &)
          -> NGIN::Core::CoreResult<
              NGIN::Memory::Shared<NavigationScopedService>> {
        return NGIN::Memory::MakeShared<NavigationScopedService>(
            navigationMetrics);
      });
  if (!pageBuilder.AddPage<HostedHomePage, NavigationViewModel>(
          {.id = "hosted-home", .displayName = "Hosted home"},
          [](Composer &composer, NavigationViewModel &,
             const NoNavigationParameter &) {
            composer.Leaf(ElementType::Border, "hosted-home-content");
          }) ||
      !pageBuilder.UsePage<HostedDetailPage, NavigationViewModel,
                           HostedDetailParameter>(
          {.id = "hosted-detail", .displayName = "Hosted detail"},
          [](Composer &composer, NavigationViewModel &,
             const HostedDetailParameter &) {
            composer.Leaf(ElementType::Border, "hosted-detail-content");
          }) ||
      !pageBuilder.UsePage<HostedMissingPage, UnregisteredViewModel>(
          {.id = "hosted-missing"}, [](Composer &, UnregisteredViewModel &,
                                       const NoNavigationParameter &) {})) {
    return Fail("Failed to register hosted pages");
  }

  auto hosting = ConfigureUIHosting(
      *builder,
      UIHostingCreateInfo{
          .application =
              ApplicationCreateInfo{
                  .platform = std::make_unique<Testing::TestPlatformBackend>(),
                  .renderer = std::move(renderer),
                  .applicationName = NGIN::Text::String{"Hosting tests"},
              },
          .maximumWait = std::chrono::milliseconds{10},
      });
  if (!hosting) {
    return Fail("Failed to configure UI hosting");
  }

  builder->SetApplicationName("NGIN.UI.Hosting.Tests");
  auto built = builder->Build();
  if (!built) {
    return Fail("Failed to build hosted application");
  }
  auto started = built.Value()->Start();
  if (!started) {
    return Fail("Failed to start hosted application");
  }
  if (!hosting.Value().services ||
      hosting.Value().services->ActiveScopeCount() != 1) {
    return Fail("Hosted application scope was not created");
  }

  auto firstWindow = hosting.Value().services->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Hosted.First"},
      .title = NGIN::Text::String{"First hosted window"},
      .initialSize = PixelSize{160, 90},
  });
  auto secondWindow = hosting.Value().services->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Hosted.Second"},
      .title = NGIN::Text::String{"Second hosted window"},
      .initialSize = PixelSize{160, 90},
  });
  if (!firstWindow || !secondWindow) {
    return Fail("Failed to create hosted windows");
  }

  const auto navigationScopeBaseline =
      hosting.Value().services->ActiveScopeCount();
  {
    auto activationContext =
        CreateHostedNavigationContext(hosting.Value(), secondWindow.Value());
    if (!activationContext) {
      return Fail("Failed to create hosted navigation context");
    }
    NavigationService navigation{
        pages, activationContext.Value(), {.region = "Hosted.Content"}};
    if (!navigation.Start<HostedHomePage>() ||
        !navigation.Navigate<HostedDetailPage>(HostedDetailParameter{42}) ||
        navigation.StackDepth() != 2 ||
        hosting.Value().services->ActiveScopeCount() !=
            navigationScopeBaseline + 2) {
      return Fail("Hosted navigation did not create one scope per entry");
    }
    const auto failed = navigation.Navigate<HostedMissingPage>();
    if (failed || navigation.StackDepth() != 2) {
      return Fail("Hosted navigation activation failure did not roll back");
    }
    Composer composed;
    navigation.Compose(composed);
    if (composed.Declarations().size() != 2 ||
        composed.Declarations().front().properties.visibility !=
            ElementVisibility::Collapsed ||
        composed.Declarations().back().properties.visibility !=
            ElementVisibility::Visible) {
      return Fail("Hosted navigation host did not retain its page stack");
    }
    if (!navigation.Back() || !navigation.Clear() ||
        !PumpUntil(hosting.Value().runtime->UI(), [&] {
          return hosting.Value().services->ActiveScopeCount() ==
                 navigationScopeBaseline;
        })) {
      return Fail("Hosted navigation did not release removed page scopes");
    }
  }
  if (navigationMetrics.servicesCreated != 2 ||
      navigationMetrics.servicesDestroyed != 2) {
    return Fail("Hosted navigation leaked page-scoped services");
  }

  auto firstPage = firstWindow.Value().CreatePageScope("First.Page");
  auto secondPage = secondWindow.Value().CreatePageScope("Second.Page");
  if (!firstPage || !secondPage) {
    return Fail("Failed to create hosted page scopes");
  }
  auto firstDependency = firstPage.Value().ResolveRequired<ScopedPageService>();
  auto reusedDependency =
      firstPage.Value().ResolveRequired<ScopedPageService>();
  auto isolatedDependency =
      secondPage.Value().ResolveRequired<ScopedPageService>();
  if (!firstDependency || !reusedDependency || !isolatedDependency ||
      firstDependency.Value().Get() != reusedDependency.Value().Get() ||
      firstDependency.Value().Get() == isolatedDependency.Value().Get()) {
    return Fail("Page-scoped services were not reused and isolated correctly");
  }

  auto activation = hosting.Value().services->CreateActivationScope(
      secondPage.Value(), "Second.Page.Activation");
  if (!activation ||
      activation.Value().Kind() != NGIN::Core::ServiceScopeKind::Activation) {
    return Fail("Transient activation scope was not created");
  }
  const auto scopeDiagnostics = built.Value()->GetServices()->Diagnostics();
  const auto hasScopeKind = [&](const NGIN::Core::ServiceScopeKind kind) {
    return std::any_of(
        scopeDiagnostics.scopes.begin(), scopeDiagnostics.scopes.end(),
        [kind](const auto &scope) { return scope.kind == kind; });
  };
  if (!hasScopeKind(NGIN::Core::ServiceScopeKind::Application) ||
      !hasScopeKind(NGIN::Core::ServiceScopeKind::Window) ||
      !hasScopeKind(NGIN::Core::ServiceScopeKind::Page) ||
      !hasScopeKind(NGIN::Core::ServiceScopeKind::Activation)) {
    return Fail("Hosted scope kinds were not reported by Core diagnostics");
  }
  if (!activation.Value().End()) {
    return Fail("Transient activation scope did not close");
  }

  auto missingPage = secondWindow.Value().CreatePageScope("Missing.Page");
  if (!missingPage) {
    return Fail("Failed to create missing-service test scope");
  }
  HostedViewModelHost<UnregisteredViewModel> missingHost{
      hosting.Value().runtime->UI().CreateTaskContext(
          *secondWindow.Value().UI()),
      std::move(missingPage).Value()};
  const auto missing = missingHost.Mount();
  if (missing ||
      missing.Error().code != NGIN::Core::KernelErrorCode::NotFound ||
      missing.Error().module.empty()) {
    return Fail("ViewModel resolution failure was not structured");
  }

  auto failingPage = secondWindow.Value().CreatePageScope("Failing.Page");
  if (!failingPage) {
    return Fail("Failed to create activation-failure test scope");
  }
  HostedViewModelHost<FailingViewModel> failingHost{
      hosting.Value().runtime->UI().CreateTaskContext(
          *secondWindow.Value().UI()),
      std::move(failingPage).Value()};
  const auto failedActivation = failingHost.Mount();
  if (failedActivation ||
      failedActivation.Error().code !=
          NGIN::Core::KernelErrorCode::ServiceRegistrationFailure ||
      failedActivation.Error().module != "FailingViewModel" ||
      failedActivation.Error().dependencyPath !=
          "FailingViewModel -> TestDependency") {
    return Fail("ViewModel activation failure lost its structured error");
  }

  firstDependency.Value().Reset();
  reusedDependency.Value().Reset();
  isolatedDependency.Value().Reset();

  HostedViewModelHost<HostedPageViewModel> firstHost{
      hosting.Value().runtime->UI().CreateTaskContext(
          *firstWindow.Value().UI()),
      std::move(firstPage).Value()};
  HostedViewModelHost<HostedPageViewModel> secondHost{
      hosting.Value().runtime->UI().CreateTaskContext(
          *secondWindow.Value().UI()),
      std::move(secondPage).Value()};
  auto firstMounted = firstHost.Mount();
  auto secondMounted = secondHost.Mount();
  if (!firstMounted || !secondMounted) {
    return Fail("DI-created ViewModels did not mount");
  }
  const auto firstServiceId = firstMounted.Value()->serviceId;
  firstMounted.Value().Reset();
  secondMounted.Value().Reset();

  firstHost.Unmount();
  if (!PumpUntil(hosting.Value().runtime->UI(),
                 [&] { return !firstHost.IsClosing(); })) {
    return Fail("Rapid ViewModel replacement did not drain cancellation");
  }
  const auto viewModelMarker = "viewmodel-" + std::to_string(firstServiceId);
  const auto serviceMarker = "service-" + std::to_string(firstServiceId);
  const auto viewModelDestroyed = std::find(
      metrics.destroyed.begin(), metrics.destroyed.end(), viewModelMarker);
  const auto serviceDestroyed = std::find(
      metrics.destroyed.begin(), metrics.destroyed.end(), serviceMarker);
  if (viewModelDestroyed == metrics.destroyed.end() ||
      serviceDestroyed == metrics.destroyed.end() ||
      viewModelDestroyed >= serviceDestroyed) {
    return Fail("ViewModel was not released before its page-scoped service");
  }

  auto closePage = firstWindow.Value().CreatePageScope("Window.Close.Page");
  if (!closePage) {
    return Fail("Failed to create window-close test page scope");
  }
  HostedViewModelHost<HostedPageViewModel> windowCloseHost{
      hosting.Value().runtime->UI().CreateTaskContext(
          *firstWindow.Value().UI()),
      std::move(closePage).Value()};
  auto closeMounted = windowCloseHost.Mount();
  if (!closeMounted) {
    return Fail("Failed to mount window-close test ViewModel");
  }
  closeMounted.Value().Reset();
  if (!hosting.Value().runtime->UI().CloseWindow(*firstWindow.Value().UI()) ||
      !hosting.Value().services->ReconcileClosedWindows() ||
      !PumpUntil(hosting.Value().runtime->UI(),
                 [&] { return !windowCloseHost.IsClosing(); })) {
    return Fail("Window close did not drain its ViewModel and page scope");
  }
  if (!secondHost.IsMounted()) {
    return Fail("Closing one window affected another window's ViewModel");
  }

  secondWindow.Value().UI()->SetContent([](Composer &composer) {
    NodeProperties properties{};
    properties.layout.preferredSize = Size{80.0F, 40.0F};
    properties.paintsBackground = true;
    properties.background = Color{0.2F, 0.5F, 0.8F, 1.0F};
    composer.Leaf(ElementType::Rectangle, properties, "content");
  });
  auto runtimeService = hosting.Value().runtime;
  auto posted = hosting.Value().dispatcher->Post(
      [runtimeService] { runtimeService->UI().RequestExit(); });
  if (!posted) {
    return Fail("Failed to post hosted shutdown");
  }

  auto ran = built.Value()->Run();
  if (!ran) {
    return Fail("Hosted UI run loop failed");
  }
  if (hosting.Value().services->AcceptsWork() ||
      hosting.Value().services->ActiveScopeCount() != 0 ||
      secondHost.IsMounted() || secondHost.IsClosing()) {
    std::cerr << "accepts=" << hosting.Value().services->AcceptsWork()
              << " scopes=" << hosting.Value().services->ActiveScopeCount()
              << " mounted=" << secondHost.IsMounted()
              << " closing=" << secondHost.IsClosing()
              << " activeTasks=" << secondHost.ActiveTaskStatus().activeCount
              << " cleanupTasks=" << secondHost.CleanupTaskStatus().activeCount
              << " deactivated=" << metrics.deactivated
              << " cleanupCompleted=" << metrics.cleanupCompleted << '\n';
    return Fail("Application shutdown did not drain hosted UI scopes");
  }
  const auto rendered = std::any_of(
      rendererObserver->Surfaces().begin(), rendererObserver->Surfaces().end(),
      [](const auto &surface) {
        return surface.renderCount != 0 && surface.presentCount != 0;
      });
  if (!rendered || rendererObserver->WaitIdleCount() != 1) {
    return Fail("Hosted UI did not render and wait for renderer shutdown");
  }
  if (metrics.activated != 3 || metrics.deactivated != 3 ||
      metrics.cleanupCompleted == 0) {
    return Fail("Hosted ViewModel lifecycle callbacks were incomplete");
  }

  auto coreOwner = NGIN::Memory::MakeShared<int>(42);
  auto coreTicket = NGIN::Memory::MakeTicket(coreOwner);
  auto standardOwner = ToStdShared(std::move(coreOwner));
  if (!standardOwner || *standardOwner != 42 || coreTicket.Expired()) {
    return Fail("Core-to-standard shared ownership bridge failed");
  }
  standardOwner.reset();
  if (!coreTicket.Expired()) {
    return Fail("Core shared owner outlived its standard alias");
  }
  auto standardSource = std::make_shared<int>(7);
  auto standardWeak = std::weak_ptr<int>{standardSource};
  auto coreAlias = ToCoreShared(std::move(standardSource));
  if (!coreAlias || *coreAlias != 7 || standardWeak.expired()) {
    return Fail("Standard-to-Core shared ownership bridge failed");
  }
  coreAlias.Reset();
  if (!standardWeak.expired()) {
    return Fail("Standard shared owner outlived its Core alias");
  }

  return 0;
}
