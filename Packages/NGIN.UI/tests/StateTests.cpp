#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/State.hpp>

#include <utility>
#include <vector>

TEST_CASE("state notifies subscribers and schedules scoped invalidation") {
  using namespace NGIN::UI;

  std::vector<int> observed;
  std::vector<InvalidationKind> invalidations;
  State<int> state{
      10,
      [&](const InvalidationKind kind) { invalidations.push_back(kind); },
      InvalidationKind::Compose,
  };
  auto subscription =
      state.Subscribe([&](const int &value) { observed.push_back(value); });

  REQUIRE(state.Get() == 10);
  REQUIRE(state.Set(20));
  REQUIRE(state.Get() == 20);
  REQUIRE(observed == std::vector<int>{20});
  REQUIRE(invalidations == std::vector{InvalidationKind::Compose});

  REQUIRE_FALSE(state.Set(20));
  REQUIRE(observed.size() == 1);
  REQUIRE(invalidations.size() == 1);

  REQUIRE(state.Update([](int &value) { value += 2; }));
  REQUIRE(state.Get() == 22);
  REQUIRE(observed == std::vector<int>{20, 22});
}

TEST_CASE("subscriptions are move-only cancellable and safe during dispatch") {
  using namespace NGIN::UI;

  State<int> state{0};
  NGIN::UIntSize firstCalls = 0;
  NGIN::UIntSize secondCalls = 0;
  Subscription first;
  first = state.Subscribe([&](const int &) {
    ++firstCalls;
    first.Cancel();
  });
  auto second = state.Subscribe([&](const int &) { ++secondCalls; });

  REQUIRE(state.Set(1));
  REQUIRE(firstCalls == 1);
  REQUIRE(secondCalls == 1);
  REQUIRE_FALSE(first);

  REQUIRE(state.Set(2));
  REQUIRE(firstCalls == 1);
  REQUIRE(secondCalls == 2);

  auto moved = std::move(second);
  REQUIRE_FALSE(second);
  REQUIRE(moved);
  moved.Cancel();
  REQUIRE(state.Set(3));
  REQUIRE(secondCalls == 2);
}

TEST_CASE("bindings provide typed state access subscription and validation") {
  using namespace NGIN::UI;

  State<int> state{4};
  auto binding = Bind(state);
  REQUIRE(binding);
  REQUIRE(binding.IsWritable());
  REQUIRE(binding.Get() == 4);

  int observed = 0;
  auto subscription =
      binding.Subscribe([&](const int &value) { observed = value; });
  REQUIRE(binding.Set(8).HasValue());
  REQUIRE(binding.Get() == 8);
  REQUIRE(observed == 8);

  auto positive =
      binding.WithValidation([](const int &value) -> UIResult<void> {
        if (value <= 0) {
          return MakeUIError(UIErrorCode::InvalidArgument,
                             "Value must be positive", "NGIN.UI.Tests",
                             "Validate");
        }
        return {};
      });

  auto rejected = positive.Set(-1);
  REQUIRE_FALSE(rejected.HasValue());
  REQUIRE(rejected.Error().code == UIErrorCode::InvalidArgument);
  REQUIRE(binding.Get() == 8);
  REQUIRE(positive.Set(12).HasValue());
  REQUIRE(binding.Get() == 12);
}

TEST_CASE("custom bindings can wrap model properties") {
  using namespace NGIN::UI;

  struct Model final {
    int value{3};
  } model;

  Binding<int> binding{
      [&]() -> const int & { return model.value; },
      [&](int value) -> UIResult<void> {
        model.value = value;
        return {};
      },
  };

  REQUIRE(binding.Get() == 3);
  REQUIRE(binding.Set(7).HasValue());
  REQUIRE(model.value == 7);
  REQUIRE_FALSE(binding.Subscribe([](const int &) {}));
}
