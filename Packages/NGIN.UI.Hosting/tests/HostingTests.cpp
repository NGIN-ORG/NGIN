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
          [](NavigationViewModel &,
             const NoNavigationParameter &) {
            NodeProperties properties{};
            properties.semantics.identifier = "hosted-home-content";
            return Views::Box(ElementType::Border, {}, properties);
          }) ||
      !pageBuilder.UsePage<HostedDetailPage, NavigationViewModel,
                           HostedDetailParameter>(
          {.id = "hosted-detail", .displayName = "Hosted detail"},
          [](NavigationViewModel &,
             const HostedDetailParameter &) {
            NodeProperties properties{};
            properties.semantics.identifier = "hosted-detail-content";
            return Views::Box(ElementType::Border, {}, properties);
          }) ||
      !pageBuilder.UsePage<HostedMissingPage, UnregisteredViewModel>(
          {.id = "hosted-missing"}, [](UnregisteredViewModel &,
                                       const NoNavigationParameter &) { return Views::View{}; })) {
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
  auto started = built.value()->Start();
  if (!started) {
    return Fail("Failed to start hosted application");
  }
  if (!hosting.value().services ||
      hosting.value().services->ActiveScopeCount() != 1) {
    return Fail("Hosted application scope was not created");
  }

  auto firstWindow = hosting.value().services->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Hosted.First"},
      .title = NGIN::Text::String{"First hosted window"},
      .initialSize = PixelSize{160, 90},
  });
  auto secondWindow = hosting.value().services->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Hosted.Second"},
      .title = NGIN::Text::String{"Second hosted window"},
      .initialSize = PixelSize{160, 90},
  });
  if (!firstWindow || !secondWindow) {
    return Fail("Failed to create hosted windows");
  }

  const auto navigationScopeBaseline =
      hosting.value().services->ActiveScopeCount();
  {
    auto activationContext =
        CreateHostedNavigationContext(hosting.value(), secondWindow.value());
    if (!activationContext) {
      return Fail("Failed to create hosted navigation context");
    }
    NavigationService navigation{
        pages, activationContext.value(), {.region = "Hosted.Content"}};
    if (!navigation.Start<HostedHomePage>() ||
        !navigation.Navigate<HostedDetailPage>(HostedDetailParameter{42}) ||
        navigation.StackDepth() != 2 ||
        hosting.value().services->ActiveScopeCount() !=
            navigationScopeBaseline + 2) {
      return Fail("Hosted navigation did not create one scope per entry");
    }
    const auto failed = navigation.Navigate<HostedMissingPage>();
    if (failed || navigation.StackDepth() != 2) {
      return Fail("Hosted navigation activation failure did not roll back");
    }
    Surface region;
    region.SetContent(navigation.ContentView(), Views::ViewEnvironment{hosting.value().runtime->Text()});
    if (!region.Update({160, 90}, 1)) return Fail("Hosted pages did not render");
    const auto &tree = region.Tree();
    const auto home = tree.FindBySemanticIdentifier("hosted-home-content");
    const auto detail = tree.FindBySemanticIdentifier("hosted-detail-content");
    if (!home || !detail ||
        tree.Get(tree.Get(home)->parent)->properties.visibility != ElementVisibility::Collapsed ||
        tree.Get(tree.Get(detail)->parent)->properties.visibility != ElementVisibility::Visible) {
      return Fail("Hosted navigation host did not retain its page stack");
    }
    if (!navigation.Back() || !navigation.Clear() || !region.Update({160, 90}, 1) ||
        !PumpUntil(hosting.value().runtime->UI(), [&] {
          return hosting.value().services->ActiveScopeCount() ==
                 navigationScopeBaseline;
        })) {
      return Fail("Hosted navigation did not release removed page scopes");
    }
  }
  if (navigationMetrics.servicesCreated != 2 ||
      navigationMetrics.servicesDestroyed != 2) {
    return Fail("Hosted navigation leaked page-scoped services");
  }

  auto firstPage = firstWindow.value().CreatePageScope("First.Page");
  auto secondPage = secondWindow.value().CreatePageScope("Second.Page");
  if (!firstPage || !secondPage) {
    return Fail("Failed to create hosted page scopes");
  }
  auto firstDependency = firstPage.value().ResolveRequired<ScopedPageService>();
  auto reusedDependency =
      firstPage.value().ResolveRequired<ScopedPageService>();
  auto isolatedDependency =
      secondPage.value().ResolveRequired<ScopedPageService>();
  if (!firstDependency || !reusedDependency || !isolatedDependency ||
      firstDependency.value().Get() != reusedDependency.value().Get() ||
      firstDependency.value().Get() == isolatedDependency.value().Get()) {
    return Fail("Page-scoped services were not reused and isolated correctly");
  }

  auto activation = hosting.value().services->CreateActivationScope(
      secondPage.value(), "Second.Page.Activation");
  if (!activation ||
      activation.value().Kind() != NGIN::Core::ServiceScopeKind::Activation) {
    return Fail("Transient activation scope was not created");
  }
  const auto scopeDiagnostics = built.value()->GetServices()->Diagnostics();
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
  if (!activation.value().End()) {
    return Fail("Transient activation scope did not close");
  }

  auto missingPage = secondWindow.value().CreatePageScope("Missing.Page");
  if (!missingPage) {
    return Fail("Failed to create missing-service test scope");
  }
  HostedViewModelHost<UnregisteredViewModel> missingHost{
      hosting.value().runtime->UI().CreateTaskContext(
          *secondWindow.value().UI()),
      std::move(missingPage).value()};
  const auto missing = missingHost.Mount();
  if (missing ||
      missing.error().code != NGIN::Core::KernelErrorCode::NotFound ||
      missing.error().module.empty()) {
    return Fail("ViewModel resolution failure was not structured");
  }

  auto failingPage = secondWindow.value().CreatePageScope("Failing.Page");
  if (!failingPage) {
    return Fail("Failed to create activation-failure test scope");
  }
  HostedViewModelHost<FailingViewModel> failingHost{
      hosting.value().runtime->UI().CreateTaskContext(
          *secondWindow.value().UI()),
      std::move(failingPage).value()};
  const auto failedActivation = failingHost.Mount();
  if (failedActivation ||
      failedActivation.error().code !=
          NGIN::Core::KernelErrorCode::ServiceRegistrationFailure ||
      failedActivation.error().module != "FailingViewModel" ||
      failedActivation.error().dependencyPath !=
          "FailingViewModel -> TestDependency") {
    return Fail("ViewModel activation failure lost its structured error");
  }

  firstDependency.value().Reset();
  reusedDependency.value().Reset();
  isolatedDependency.value().Reset();

  HostedViewModelHost<HostedPageViewModel> firstHost{
      hosting.value().runtime->UI().CreateTaskContext(
          *firstWindow.value().UI()),
      std::move(firstPage).value()};
  HostedViewModelHost<HostedPageViewModel> secondHost{
      hosting.value().runtime->UI().CreateTaskContext(
          *secondWindow.value().UI()),
      std::move(secondPage).value()};
  auto firstMounted = firstHost.Mount();
  auto secondMounted = secondHost.Mount();
  if (!firstMounted || !secondMounted) {
    return Fail("DI-created ViewModels did not mount");
  }
  const auto firstServiceId = firstMounted.value()->serviceId;
  firstMounted.value().Reset();
  secondMounted.value().Reset();

  firstHost.Unmount();
  if (!PumpUntil(hosting.value().runtime->UI(),
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

  auto closePage = firstWindow.value().CreatePageScope("Window.Close.Page");
  if (!closePage) {
    return Fail("Failed to create window-close test page scope");
  }
  HostedViewModelHost<HostedPageViewModel> windowCloseHost{
      hosting.value().runtime->UI().CreateTaskContext(
          *firstWindow.value().UI()),
      std::move(closePage).value()};
  auto closeMounted = windowCloseHost.Mount();
  if (!closeMounted) {
    return Fail("Failed to mount window-close test ViewModel");
  }
  closeMounted.value().Reset();
  if (!hosting.value().runtime->UI().CloseWindow(*firstWindow.value().UI()) ||
      !hosting.value().services->ReconcileClosedWindows() ||
      !PumpUntil(hosting.value().runtime->UI(),
                 [&] { return !windowCloseHost.IsClosing(); })) {
    return Fail("Window close did not drain its ViewModel and page scope");
  }
  if (!secondHost.IsMounted()) {
    return Fail("Closing one window affected another window's ViewModel");
  }

  NodeProperties content{};
  content.layout.preferredSize = Size{80.0F, 40.0F};
  content.paintsBackground = true;
  content.background = Color{0.2F, 0.5F, 0.8F, 1.0F};
  secondWindow.value().UI()->SetContent(
      Views::Box(ElementType::Rectangle, {}, content).Key("content"),
      Views::ViewEnvironment{hosting.value().runtime->Text()});
  auto runtimeService = hosting.value().runtime;
  auto &owner = runtimeService->UI().BackgroundTasks();
  NGIN::Async::CancellationToken backgroundToken;
  if (!owner.Transfer([&backgroundToken](NGIN::Async::TaskContext &context)
                          -> NGIN::Async::Task<void> {
        backgroundToken = context.GetCancellationToken();
        co_await context.Delay(NGIN::Units::Seconds(60));
      }) ||
      !runtimeService->UI().PumpOnce()) {
    return Fail("Failed to start hosted background work");
  }
  auto posted = hosting.value().dispatcher->Post(
      [runtimeService] { runtimeService->UI().RequestExit(); });
  if (!posted) {
    return Fail("Failed to post hosted shutdown");
  }

  auto ran = built.value()->Run();
  if (!ran) {
    return Fail("Hosted UI run loop failed");
  }
  if (owner.Pending() != 0 || !backgroundToken.IsCancellationRequested() ||
      backgroundToken.GetStopReason() != NGIN::Async::StopReason::RuntimeShutdown ||
      owner.TakeResult().HasError()) {
    return Fail("Hosted shutdown did not cancel and join application work");
  }
  if (hosting.value().services->AcceptsWork() ||
      hosting.value().services->ActiveScopeCount() != 0 ||
      secondHost.IsMounted() || secondHost.IsClosing()) {
    std::cerr << "accepts=" << hosting.value().services->AcceptsWork()
              << " scopes=" << hosting.value().services->ActiveScopeCount()
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
