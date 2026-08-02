#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/State.hpp>

#include <utility>
#include <vector>

namespace {
template <typename T>
concept HasIntegerSetter = requires(T value) { value.Set(1); };
} // namespace

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

TEST_CASE("read only bindings observe state without exposing a setter") {
  using namespace NGIN::UI;

  State<int> state{7};
  auto readOnly = Observe(state);
  static_assert(!HasIntegerSetter<ReadOnlyBinding<int>>);
  REQUIRE(readOnly.Get() == 7);

  int observed = 0;
  auto subscription =
      readOnly.Subscribe([&](const int &value) { observed = value; });
  REQUIRE(state.Set(9));
  REQUIRE(readOnly.Get() == 9);
  REQUIRE(observed == 9);

  auto fromBinding = Bind(state).AsReadOnly();
  REQUIRE(fromBinding.Get() == 9);
}

TEST_CASE("computed state follows explicit typed dependencies") {
  using namespace NGIN::UI;

  State<int> left{2};
  State<int> right{3};
  int computations = 0;
  ComputedState<int> sum{
      [&] {
        ++computations;
        return left.Get() + right.Get();
      },
      {DependOn(left), DependOn(right)},
  };
  REQUIRE(sum.Get() == 5);

  std::vector<int> observed;
  auto subscription =
      sum.Subscribe([&](const int &value) { observed.push_back(value); });
  REQUIRE(left.Set(4));
  REQUIRE(sum.Get() == 7);
  REQUIRE(observed == std::vector<int>{7});

  const auto beforeBatch = computations;
  {
    StateBatch batch;
    REQUIRE(left.Set(10));
    REQUIRE(right.Set(20));
    REQUIRE(sum.Get() == 7);
  }
  REQUIRE(sum.Get() == 30);
  REQUIRE(computations == beforeBatch + 1);
  REQUIRE(observed == std::vector<int>{7, 30});
}

TEST_CASE("nested state batches publish final values once") {
  using namespace NGIN::UI;

  State<int> value{0};
  std::vector<int> observed;
  auto subscription =
      value.Subscribe([&](const int &next) { observed.push_back(next); });
  {
    StateBatch outer;
    REQUIRE(value.Set(1));
    {
      StateBatch inner;
      REQUIRE(value.Set(2));
    }
    REQUIRE(value.Set(3));
    REQUIRE(observed.empty());
  }
  REQUIRE(observed == std::vector<int>{3});
}

TEST_CASE("computed dependency graphs reject cycles and retain old wiring") {
  using namespace NGIN::UI;

  State<int> source{1};
  ComputedState<int> first{[&] { return source.Get() + 1; },
                           {DependOn(source)}};
  ComputedState<int> second{[&] { return first.Get() + 1; },
                            {DependOn(first.AsReadOnly())}};
  REQUIRE(first.Get() == 2);
  REQUIRE(second.Get() == 3);

  auto cycle = first.SetDependencies({DependOn(second.AsReadOnly())});
  REQUIRE_FALSE(cycle.HasValue());
  REQUIRE(cycle.Error().code == UIErrorCode::InvalidArgument);

  REQUIRE(source.Set(4));
  REQUIRE(first.Get() == 5);
  REQUIRE(second.Get() == 6);
}

TEST_CASE("computed bindings safely retain their observable storage") {
  using namespace NGIN::UI;

  ReadOnlyBinding<int> retained;
  {
    State<int> source{4};
    ComputedState<int> doubled{[&] { return source.Get() * 2; },
                               {DependOn(source)}};
    retained = doubled.AsReadOnly();
    REQUIRE(retained.Get() == 8);
  }
  REQUIRE(retained.Get() == 8);
}
