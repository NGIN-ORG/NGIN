#include <catch2/catch_test_macros.hpp>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/AsyncPresentation.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
#include <NGIN/UI/ViewModel.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

namespace {
[[nodiscard]] auto CompleteAfterYield(NGIN::Async::TaskContext &context,
                                      int &completions)
    -> NGIN::UI::ViewModelTaskScope::Task {
  co_await context.YieldNow();
  ++completions;
}

[[nodiscard]] auto FailAfterYield(NGIN::Async::TaskContext &context)
    -> NGIN::UI::ViewModelTaskScope::Task {
  co_await context.YieldNow();
  co_await NGIN::Async::DomainFailure(NGIN::UI::CommandError{
      .kind = NGIN::UI::CommandErrorKind::Domain,
      .code = NGIN::Text::String{"load-failed"},
      .message = NGIN::Text::String{"Loading failed"},
  });
}

struct LifecycleMetrics final {
  int activated{0};
  int activationCompleted{0};
  int deactivated{0};
  int cleanupCompleted{0};
  std::vector<NGIN::Text::String> keys{};
};

struct PlainLifecycleViewModel final {
  PlainLifecycleViewModel(NGIN::Text::String viewModelKey,
                          std::shared_ptr<LifecycleMetrics> lifecycleMetrics)
      : key(std::move(viewModelKey)), metrics(std::move(lifecycleMetrics)) {
    metrics->keys.push_back(key);
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

  NGIN::Text::String key{};
  std::shared_ptr<LifecycleMetrics> metrics{};
};
} // namespace

TEST_CASE("ViewModel task scopes observe success failure and cancellation") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  ViewModelTaskScope scope{context};
  int completions = 0;
  std::vector<ViewModelTaskOutcome> outcomes;

  auto success = scope.Start(
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, completions);
      },
      [&](const ViewModelTaskOutcome &outcome) {
        outcomes.push_back(outcome);
      });
  auto failure = scope.Start(FailAfterYield);
  auto canceled = scope.Start([&](NGIN::Async::TaskContext &runContext) {
    return CompleteAfterYield(runContext, completions);
  });
  canceled.Cancel();

  REQUIRE(success.IsRunning());
  REQUIRE(failure.IsRunning());
  REQUIRE(scope.Status().activeCount == 3);
  scheduler.RunUntilIdle();

  REQUIRE(completions == 1);
  REQUIRE_FALSE(success.IsRunning());
  REQUIRE_FALSE(canceled.IsRunning());
  REQUIRE(scope.Status().activeCount == 0);
  REQUIRE(scope.Status().succeededCount == 1);
  REQUIRE(scope.Status().failedCount == 1);
  REQUIRE(scope.Status().canceledCount == 1);
  REQUIRE(outcomes.size() == 1);
  REQUIRE(outcomes.front().kind == ViewModelTaskOutcomeKind::Succeeded);
  scope.CancelAll();
  REQUIRE_FALSE(scope.Status().acceptsWork);
  REQUIRE_FALSE(scope.Start(FailAfterYield));
}

TEST_CASE(
    "closing a ViewModel task scope rejects work and ignores late callbacks") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  int completions = 0;
  int observations = 0;
  ReadOnlyBinding<ViewModelTaskStatus> retained;
  {
    ViewModelTaskScope scope{context};
    retained = scope.StatusBinding();
    static_cast<void>(scope.Start(
        [&](NGIN::Async::TaskContext &runContext) {
          return CompleteAfterYield(runContext, completions);
        },
        [&](const ViewModelTaskOutcome &) { ++observations; }));
  }
  scheduler.RunUntilIdle();

  REQUIRE(completions == 0);
  REQUIRE(observations == 0);
  REQUIRE_FALSE(retained.Get().acceptsWork);
}

#if NGIN_ASYNC_HAS_EXCEPTIONS
TEST_CASE("ViewModel task scopes retain observer exceptions as faults") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  ViewModelTaskScope scope{context};
  int completions = 0;
  static_cast<void>(scope.Start(
      [&](NGIN::Async::TaskContext &runContext) {
        return CompleteAfterYield(runContext, completions);
      },
      [](const ViewModelTaskOutcome &) {
        throw std::runtime_error{"observer failed"};
      }));
  scheduler.RunUntilIdle();

  REQUIRE(completions == 1);
  REQUIRE(scope.Status().failedCount == 1);
  REQUIRE(scope.Status().lastOutcome.kind == ViewModelTaskOutcomeKind::Fault);
  REQUIRE(scope.Status().lastOutcome.error.has_value());
}
#endif

TEST_CASE("keyed ViewModel hosts reuse replace and clean up plain types") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  auto metrics = std::make_shared<LifecycleMetrics>();
  KeyedViewModelHost<PlainLifecycleViewModel> host{
      context, [metrics](const NGIN::Text::String &key,
                         const ViewModelServiceResolver &) {
        return std::make_shared<PlainLifecycleViewModel>(key, metrics);
      }};

  auto first = host.Show(NGIN::Text::String{"first"});
  REQUIRE(first.HasValue());
  auto reused = host.Show(NGIN::Text::String{"first"});
  REQUIRE(reused.HasValue());
  REQUIRE(first.Value() == reused.Value());
  REQUIRE(metrics->activated == 1);

  auto second = host.Show(NGIN::Text::String{"second"});
  REQUIRE(second.HasValue());
  REQUIRE(second.Value() != first.Value());
  REQUIRE(metrics->activated == 2);
  REQUIRE(metrics->deactivated == 1);
  scheduler.RunUntilIdle();
  REQUIRE(metrics->activationCompleted == 1);
  REQUIRE(metrics->cleanupCompleted == 1);
  REQUIRE(host.ActiveTaskStatus().succeededCount == 1);
  REQUIRE(host.CleanupTaskStatus().succeededCount == 1);

  host.Hide();
  scheduler.RunUntilIdle();
  REQUIRE_FALSE(host.IsMounted());
  REQUIRE(metrics->deactivated == 2);
  REQUIRE(metrics->cleanupCompleted == 2);
  REQUIRE(host.CleanupTaskStatus().succeededCount == 2);
}

TEST_CASE("rapid ViewModel replacement prevents stale activation") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  auto metrics = std::make_shared<LifecycleMetrics>();
  KeyedViewModelHost<PlainLifecycleViewModel> host{
      context, [metrics](const NGIN::Text::String &key,
                         const ViewModelServiceResolver &) {
        return std::make_shared<PlainLifecycleViewModel>(key, metrics);
      }};

  REQUIRE(host.Show(NGIN::Text::String{"one"}).HasValue());
  REQUIRE(host.Show(NGIN::Text::String{"two"}).HasValue());
  REQUIRE(host.Show(NGIN::Text::String{"three"}).HasValue());
  scheduler.RunUntilIdle();

  REQUIRE(host.CurrentKey() == NGIN::Text::String{"three"});
  REQUIRE(metrics->activated == 3);
  REQUIRE(metrics->activationCompleted == 1);
  REQUIRE(metrics->deactivated == 2);
  REQUIRE(metrics->cleanupCompleted == 2);
}

TEST_CASE("ViewModel factories can use a narrow non-owning service resolver") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  auto metrics = std::make_shared<LifecycleMetrics>();
  int service = 42;
  ViewModelServiceResolver services{[&](const std::type_index type) -> void * {
    return type == std::type_index{typeid(int)} ? &service : nullptr;
  }};
  int resolved = 0;
  KeyedViewModelHost<PlainLifecycleViewModel> host{
      context,
      [&](const NGIN::Text::String &key,
          const ViewModelServiceResolver &resolver) {
        resolved = *resolver.TryResolve<int>();
        return std::make_shared<PlainLifecycleViewModel>(key, metrics);
      },
      services};

  REQUIRE(host.Show(NGIN::Text::String{"service"}).HasValue());
  REQUIRE(resolved == 42);
}

#if NGIN_ASYNC_HAS_EXCEPTIONS
TEST_CASE("keyed ViewModel hosts report factory exceptions") {
  using namespace NGIN::UI;

  NGIN::Execution::CooperativeScheduler scheduler;
  NGIN::Async::TaskContext context{scheduler};
  KeyedViewModelHost<PlainLifecycleViewModel> host{
      context,
      [](const NGIN::Text::String &, const ViewModelServiceResolver &)
          -> std::shared_ptr<PlainLifecycleViewModel> {
        throw std::runtime_error{"factory failed"};
      }};

  const auto shown = host.Show(NGIN::Text::String{"failure"});
  REQUIRE_FALSE(shown.HasValue());
  REQUIRE(shown.Error().code == UIErrorCode::ResourceFailed);
  REQUIRE_FALSE(host.IsMounted());
}
#endif

TEST_CASE("window and application closure cancel ViewModel task scopes") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"ViewModelLifetime"},
      .title = NGIN::Text::String{"ViewModel lifetime"},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();
  int completions = 0;
  ViewModelTaskScope scope{application->CreateTaskContext(*window)};
  static_cast<void>(scope.Start([&](NGIN::Async::TaskContext &context) {
    return CompleteAfterYield(context, completions);
  }));

  REQUIRE(application->CloseWindow(*window).HasValue());
  for (auto index = 0; index < 8 && scope.Status().activeCount != 0; ++index) {
    REQUIRE(application->PumpOnce().HasValue());
  }
  REQUIRE(completions == 0);
  REQUIRE(scope.Status().lastOutcome.kind ==
          ViewModelTaskOutcomeKind::Canceled);

  auto secondWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"ApplicationViewModelLifetime"},
      .title = NGIN::Text::String{"Application ViewModel lifetime"},
  });
  REQUIRE(secondWindow.HasValue());
  ViewModelTaskScope applicationScope{
      application->CreateTaskContext(*secondWindow.Value())};
  static_cast<void>(
      applicationScope.Start([&](NGIN::Async::TaskContext &context) {
        return CompleteAfterYield(context, completions);
      }));
  application.reset();
  REQUIRE(completions == 0);
  REQUIRE(applicationScope.Status().lastOutcome.kind ==
          ViewModelTaskOutcomeKind::Canceled);
}

TEST_CASE("async presentation exposes every screen state and actions") {
  using namespace NGIN::UI;

  int retries = 0;
  int cancellations = 0;
  Command retry{[&] { ++retries; }};
  Command cancel{[&] { ++cancellations; }};
  AsyncPresentation<std::vector<int>> presentation;
  presentation.SetRetryCommand(retry.AsBinding());
  presentation.SetCancelCommand(cancel.AsBinding());

  REQUIRE(presentation.Get().kind == AsyncPresentationKind::Idle);
  presentation.SetLoading();
  REQUIRE(presentation.Get().kind == AsyncPresentationKind::Loading);
  REQUIRE(presentation.CancelCommand().Execute() == CommandInvocation::Started);
  presentation.SetContent({1, 2, 3});
  REQUIRE(presentation.Get().kind == AsyncPresentationKind::Content);
  REQUIRE(presentation.Get().content == std::vector{1, 2, 3});
  presentation.SetEmpty();
  REQUIRE(presentation.Get().kind == AsyncPresentationKind::Empty);
  presentation.SetError(CommandError{
      .code = NGIN::Text::String{"load-failed"},
      .message = NGIN::Text::String{"Try again"},
  });
  REQUIRE(presentation.Get().kind == AsyncPresentationKind::Error);
  REQUIRE(presentation.RetryCommand().Execute() == CommandInvocation::Started);
  REQUIRE(retries == 1);
  REQUIRE(cancellations == 1);
  presentation.SetIdle();
  REQUIRE(presentation.Get().kind == AsyncPresentationKind::Idle);
}
