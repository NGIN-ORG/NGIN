#include <NGIN/UI/Navigation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {
using namespace NGIN::UI;
using NGIN::F32;
using NGIN::UInt32;
using NGIN::UIntSize;

struct TestContext final : PageActivationContext {};
struct HomePage final {};
struct DetailPage final {};
struct FailingPage final {};

struct HomeViewModel final {
  int value{7};
};

struct DetailParameter final {
  int itemId{0};
};

struct DetailViewModel final {
  explicit DetailViewModel(int selected) : itemId(selected) {}
  int itemId{0};
};

template <typename T>
auto Lease(std::shared_ptr<T> value, int *closed = nullptr)
    -> UIResult<PageLease<T>> {
  return PageLease<T>{
      .viewModel = std::move(value),
      .close = closed == nullptr
                   ? NGIN::Utilities::Callable<void()>{}
                   : NGIN::Utilities::Callable<void()>{[closed] { ++*closed; }},
  };
}

auto RegisterHome(PageRegistry &registry, int *closed = nullptr,
                  NavigationService **reentrant = nullptr,
                  UIResult<NavigationChange> *nested = nullptr)
    -> UIResult<void> {
  return registry.Register<HomePage, HomeViewModel>(
      {.id = "home", .displayName = "Home", .routeName = "home"},
      [closed, reentrant, nested](PageActivationContext &,
                                  const NoNavigationParameter &,
                                  std::string_view) {
        if (reentrant != nullptr && *reentrant != nullptr &&
            nested != nullptr) {
          *nested = (*reentrant)->Navigate<DetailPage>(DetailParameter{4});
        }
        return Lease(std::make_shared<HomeViewModel>(), closed);
      },
      [](Composer &composer, HomeViewModel &viewModel,
         const NoNavigationParameter &) {
        NodeProperties properties{};
        properties.layout.preferredSize.width =
            static_cast<F32>(viewModel.value);
        composer.Leaf(ElementType::Border, properties, "home-content");
      });
}

auto RegisterDetail(PageRegistry &registry, int *closed = nullptr,
                    int *activations = nullptr) -> UIResult<void> {
  return registry.Register<DetailPage, DetailViewModel, DetailParameter>(
      {.id = "detail", .displayName = "Detail", .routeName = "item"},
      [closed, activations](PageActivationContext &,
                            const DetailParameter &parameter,
                            std::string_view) {
        if (activations != nullptr) {
          ++*activations;
        }
        return Lease(std::make_shared<DetailViewModel>(parameter.itemId),
                     closed);
      },
      [](Composer &composer, DetailViewModel &viewModel,
         const DetailParameter &parameter) {
        NodeProperties properties{};
        properties.layout.preferredSize.width =
            static_cast<F32>(viewModel.itemId + parameter.itemId);
        composer.Leaf(ElementType::Border, properties, "detail-content");
      });
}
} // namespace

TEST_CASE("PageRegistry rejects duplicate identities routes and late changes",
          "[navigation][pages]") {
  PageRegistry registry;
  REQUIRE(RegisterHome(registry));
  REQUIRE(registry.Pages().size() == 1);
  REQUIRE(registry.FindById("home") != nullptr);
  REQUIRE(registry.FindByRoute("home") != nullptr);

  const auto duplicateId =
      registry.Register<DetailPage, DetailViewModel, DetailParameter>(
          {.id = "home", .routeName = "other"},
          [](PageActivationContext &, const DetailParameter &,
             std::string_view) {
            return Lease(std::make_shared<DetailViewModel>(1));
          },
          [](Composer &, DetailViewModel &, const DetailParameter &) {});
  REQUIRE_FALSE(duplicateId);
  REQUIRE(duplicateId.Error().code == UIErrorCode::InvalidArgument);

  struct OtherPage final {};
  const auto duplicateRoute = registry.Register<OtherPage, HomeViewModel>(
      {.id = "other", .routeName = "home"},
      [](PageActivationContext &, const NoNavigationParameter &,
         std::string_view) { return Lease(std::make_shared<HomeViewModel>()); },
      [](Composer &, HomeViewModel &, const NoNavigationParameter &) {});
  REQUIRE_FALSE(duplicateRoute);

  struct MissingFactoryPage final {};
  NGIN::Utilities::Callable<UIResult<PageLease<HomeViewModel>>(
      PageActivationContext &, const NoNavigationParameter &, std::string_view)>
      missingFactory;
  const auto missing = registry.Register<MissingFactoryPage, HomeViewModel>(
      {.id = "missing-factory"}, std::move(missingFactory),
      [](Composer &, HomeViewModel &, const NoNavigationParameter &) {});
  REQUIRE_FALSE(missing);
  REQUIRE(missing.Error().code == UIErrorCode::InvalidArgument);

  registry.Freeze();
  struct LatePage final {};
  const auto late = registry.Register<LatePage, HomeViewModel>(
      {.id = "late"},
      [](PageActivationContext &, const NoNavigationParameter &,
         std::string_view) { return Lease(std::make_shared<HomeViewModel>()); },
      [](Composer &, HomeViewModel &, const NoNavigationParameter &) {});
  REQUIRE_FALSE(late);
  REQUIRE(late.Error().code == UIErrorCode::InvalidState);
}

TEST_CASE("Navigation is typed rollback safe and retains stack entries",
          "[navigation]") {
  PageRegistry registry;
  int homeClosed = 0;
  int detailClosed = 0;
  REQUIRE(RegisterHome(registry, &homeClosed));
  REQUIRE(RegisterDetail(registry, &detailClosed));
  REQUIRE(registry.Register<FailingPage, HomeViewModel>(
      {.id = "failing"},
      [](PageActivationContext &, const NoNavigationParameter &,
         std::string_view) -> UIResult<PageLease<HomeViewModel>> {
        return MakeUIError(UIErrorCode::ResourceFailed, "expected failure");
      },
      [](Composer &, HomeViewModel &, const NoNavigationParameter &) {}));

  TestContext context;
  int invalidations = 0;
  std::vector<UIntSize> depths;
  NavigationService navigation{
      registry,
      context,
      {.region = "Content", .invalidate = [&](InvalidationKind kind) {
         REQUIRE(kind == InvalidationKind::Compose);
         ++invalidations;
       }}};
  navigation.SetObserver([&](const NavigationSnapshot &value) {
    depths.push_back(value.stack.size());
  });

  REQUIRE(navigation.Start<HomePage>());
  REQUIRE(navigation.Navigate<DetailPage>(DetailParameter{42}));
  REQUIRE(navigation.StackDepth() == 2);
  REQUIRE(navigation.CanGoBack());

  Composer composer;
  navigation.Compose(composer);
  REQUIRE(composer.Declarations().size() == 2);
  REQUIRE(composer.Declarations()[0].properties.visibility ==
          ElementVisibility::Collapsed);
  REQUIRE(composer.Declarations()[1].properties.visibility ==
          ElementVisibility::Visible);
  REQUIRE(composer.Declarations()[0].key != composer.Declarations()[1].key);

  const auto failure = navigation.Navigate<FailingPage>();
  REQUIRE_FALSE(failure);
  REQUIRE(navigation.StackDepth() == 2);
  REQUIRE(detailClosed == 0);

  REQUIRE(navigation.Back());
  REQUIRE(navigation.StackDepth() == 1);
  REQUIRE(detailClosed == 1);
  REQUIRE(homeClosed == 0);
  REQUIRE(invalidations == 3);
  REQUIRE(depths == std::vector<UIntSize>{1, 2, 1});

  REQUIRE(navigation.Replace<DetailPage>(DetailParameter{8}));
  REQUIRE(navigation.StackDepth() == 1);
  REQUIRE(homeClosed == 1);
  REQUIRE(navigation.Clear());
  REQUIRE(navigation.StackDepth() == 0);
  REQUIRE(detailClosed == 2);
  REQUIRE(invalidations == 5);
  REQUIRE(depths == std::vector<UIntSize>{1, 2, 1, 1, 0});
}

TEST_CASE("Navigation cache is explicit bounded and reusable",
          "[navigation][cache]") {
  PageRegistry registry;
  int closed = 0;
  int activations = 0;
  REQUIRE(RegisterHome(registry));
  REQUIRE(RegisterDetail(registry, &closed, &activations));
  TestContext context;
  NavigationService navigation{
      registry, context, {.region = "Main", .cacheCapacity = 1}};

  REQUIRE(navigation.Start<HomePage>());
  const auto first = navigation.Navigate<DetailPage>(DetailParameter{1}, "one");
  REQUIRE(first);
  REQUIRE(navigation.Back());
  REQUIRE(navigation.Snapshot().cache.size() == 1);
  const auto restored =
      navigation.Navigate<DetailPage>(DetailParameter{999}, "one");
  REQUIRE(restored);
  REQUIRE(restored.Value().restoredFromCache);
  REQUIRE(restored.Value().entryId == first.Value().entryId);
  REQUIRE(activations == 1);

  REQUIRE(navigation.Back());
  REQUIRE(navigation.Navigate<DetailPage>(DetailParameter{2}, "two"));
  REQUIRE(navigation.Back());
  REQUIRE(navigation.Snapshot().cache.size() == 1);
  REQUIRE(closed == 1);
}

TEST_CASE("Navigation enforces scheduler keyboard back and region isolation",
          "[navigation][input]") {
  PageRegistry registry;
  REQUIRE(RegisterHome(registry));
  REQUIRE(RegisterDetail(registry));
  TestContext firstContext;
  TestContext secondContext;
  bool onScheduler = false;
  NavigationService first{
      registry, firstContext, {.region = "First", .isOnScheduler = [&] {
                                 return onScheduler;
                               }}};
  NavigationService second{registry, secondContext, {.region = "Second"}};

  const auto wrongThread = first.Start<HomePage>();
  REQUIRE_FALSE(wrongThread);
  REQUIRE(wrongThread.Error().code == UIErrorCode::WrongThread);
  onScheduler = true;
  REQUIRE(first.Start<HomePage>());
  REQUIRE(first.Navigate<DetailPage>(DetailParameter{3}));
  REQUIRE(second.Start<HomePage>());
  REQUIRE(first.StackDepth() == 2);
  REQUIRE(second.StackDepth() == 1);

  PlatformEvent back =
      KeyChanged{.logicalKey = static_cast<UInt32>(LogicalKey::Left),
                 .state = KeyState::Pressed,
                 .modifiers = static_cast<UInt32>(KeyModifierFlags::Alt)};
  REQUIRE(first.HandleBackEvent(back).Value());
  REQUIRE(first.StackDepth() == 1);
  REQUIRE(second.StackDepth() == 1);
}

TEST_CASE("Navigation rejects reentrant mutations", "[navigation]") {
  PageRegistry registry;
  NavigationService *active = nullptr;
  UIResult<NavigationChange> nested =
      MakeUIError(UIErrorCode::InvalidState, "not attempted");
  REQUIRE(RegisterHome(registry, nullptr, &active, &nested));
  REQUIRE(RegisterDetail(registry));
  TestContext context;
  NavigationService navigation{registry, context};
  active = &navigation;

  REQUIRE(navigation.Start<HomePage>());
  REQUIRE_FALSE(nested);
  REQUIRE(nested.Error().code == UIErrorCode::InvalidState);
  REQUIRE(navigation.StackDepth() == 1);
}
