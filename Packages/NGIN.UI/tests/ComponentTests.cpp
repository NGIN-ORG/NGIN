#include "HeadlessTextFixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>

#include <NGIN/UI/Components.hpp>
#include <NGIN/UI/Surface.hpp>

using namespace NGIN::UI;

using Testing::HeadlessText;

TEST_CASE("mounted components rebuild only readers and drop old conditional "
          "subscriptions") {
  HeadlessText text;
  State<bool> showLeft{true};
  State<int> left{0}, right{0};
  int reads{}, staticBuilds{};
  Surface surface;
  surface.SetContent(
      Views::Row({Views::Component([&] {
                    ++reads;
                    auto selected = showLeft.Get() ? left.Get() : right.Get();
                    return Views::Label(std::to_string(selected));
                  }),
                  Views::Component([&] {
                    ++staticBuilds;
                    return Views::Button(
                        "Apply",
                        [value = std::make_unique<int>(0)] { ++*value; });
                  })}),
      text.Environment());

  REQUIRE(surface.Update({400, 200}, 1));
  CHECK(reads == 1);
  CHECK(staticBuilds == 1);
  REQUIRE(left.Set(1));
  REQUIRE(surface.IsDirty());
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK(reads == 2);
  CHECK(staticBuilds == 1);
  CHECK(surface.Diagnostics().reconciliation.skippedSubtrees >= 1);
  REQUIRE(showLeft.Set(false));
  REQUIRE(surface.Update({400, 200}, 1));
  REQUIRE(left.Set(2));
  CHECK_FALSE(surface.IsDirty());
  REQUIRE(right.Set(3));
  CHECK(surface.IsDirty());
}

TEST_CASE("component local state follows a sibling key and rejects stale "
          "writes after unmount") {
  HeadlessText text;
  Surface surface;
  State<bool> reverse{false}, visible{true};
  Binding<int> first, second;
  auto item = [&](const char *key, Binding<int> &exposed) {
    return Views::Component([&exposed](Views::ComponentScope &scope) {
             exposed = scope.Local("count", 0);
             return Views::Label(std::to_string(exposed.Get()));
           })
        .Key(key);
  };
  auto panel = Views::Component([&] {
    if (!visible.Get())
      return Views::View{};
    auto a = item("a", first), b = item("b", second);
    return reverse.Get() ? Views::Row({b, a}) : Views::Row({a, b});
  });
  const auto baseline = CurrentSubscriptionDiagnostics().activeCount;
  surface.SetContent(panel, text.Environment());
  REQUIRE(surface.Update({400, 200}, 1));
  REQUIRE(first.Set(11));
  REQUIRE(second.Set(22));
  REQUIRE(reverse.Set(true));
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK(first.Get() == 11);
  CHECK(second.Get() == 22);
  REQUIRE(visible.Set(false));
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK_FALSE(first.Set(99));
  CHECK_FALSE(second.Set(99));
  surface.SetContent({}, text.Environment());
  CHECK(CurrentSubscriptionDiagnostics().activeCount == baseline);
}

TEST_CASE(
    "independent mounts of the same component own separate local storage") {
  HeadlessText text;
  std::vector<Binding<int>> handles;
  auto view = Views::Component([&](Views::ComponentScope &scope) {
    handles.push_back(scope.Local("count", 0));
    return Views::Label("Counter");
  });
  Surface first, second;
  first.SetContent(view, text.Environment());
  second.SetContent(view, text.Environment());
  REQUIRE(first.Update({300, 200}, 1));
  REQUIRE(second.Update({300, 200}, 1));
  REQUIRE(handles.size() == 2);
  REQUIRE(handles[0].Set(9));
  CHECK(handles[1].Get() == 0);
  first.SetContent({}, text.Environment());
  CHECK_FALSE(handles[0].Set(2));
  CHECK(handles[1].Set(3).has_value());
}

TEST_CASE("a late save completion cannot write into a reopened component") {
  HeadlessText text;
  State<bool> open{true};
  Binding<int> current;
  NGIN::Async::CancellationToken currentLifetime;
  Surface surface;
  auto editor = Views::Component([&](Views::ComponentScope &scope) {
    current = scope.Local("saved", 0);
    currentLifetime = scope.Lifetime();
    return Views::Label(std::to_string(current.Get()));
  });
  surface.SetContent(
      Views::Component([&] { return open.Get() ? editor : Views::View{}; }),
      text.Environment());
  REQUIRE(surface.Update({400, 200}, 1));

  // Worker returns a result. Its UI-thread completion holds only a binding
  // to the editor that was open when Save started.
  const auto originalLifetime = currentLifetime;
  std::promise<int> storage;
  auto finish = [original = current, result = storage.get_future()]() mutable {
    return original.Set(result.get());
  };
  REQUIRE(open.Set(false));
  REQUIRE(surface.Update({400, 200}, 1));
  REQUIRE(open.Set(true));
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK(originalLifetime.IsCancellationRequested());
  CHECK(originalLifetime.GetStopReason() ==
        NGIN::Async::StopReason::OwnerClosed);
  CHECK_FALSE(currentLifetime.IsCancellationRequested());
  storage.set_value(17);
  auto stale = finish();
  REQUIRE_FALSE(stale);
  CHECK(stale.error().code == UIErrorCode::InvalidState);
  CHECK(current.Get() == 0);
  CHECK_FALSE(surface.IsDirty());
}

TEST_CASE("writes during an initial build schedule another pass and observable "
          "getters subscribe once") {
  HeadlessText text;
  State<int> model{0};
  ReadOnlyBinding<int> adapter{[&]() -> const int & { return model.Get(); },
                               [&](StateObserver<int> observer) {
                                 return model.Subscribe(std::move(observer));
                               }};
  auto baseline = CurrentSubscriptionDiagnostics();
  int builds{};
  Surface surface;
  surface.SetContent(Views::Component([&] {
                       ++builds;
                       const auto value = adapter.Get();
                       if (value == 0)
                         CHECK(model.Set(1));
                       return Views::Label(std::to_string(value));
                     }),
                     text.Environment());
  REQUIRE(surface.Update({400, 200}, 1));
  REQUIRE(surface.IsDirty());
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK(builds == 2);
  CHECK_FALSE(surface.IsDirty());
  CHECK(CurrentSubscriptionDiagnostics().activeCount ==
        baseline.activeCount + 1);
  CHECK(CurrentSubscriptionDiagnostics().createdCount ==
        baseline.createdCount + 1);
}

TEST_CASE("changing the parent container type remounts child-local state") {
  HeadlessText text;
  State<bool> vertical{true};
  Binding<int> value;
  auto item = Views::Component([&](Views::ComponentScope &scope) {
    value = scope.Local("count", 1);
    return Views::Label("Item");
  });
  Surface surface;
  surface.SetContent(Views::Component([&] {
                       return vertical.Get() ? Views::Column({item})
                                             : Views::Row({item});
                     }),
                     text.Environment());
  REQUIRE(surface.Update({400, 200}, 1));
  auto first = value;
  REQUIRE(first.Set(8));
  REQUIRE(vertical.Set(false));
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK(value.Get() == 1);
  CHECK_FALSE(first.Set(2));
}

TEST_CASE("changing a nested component key replaces a same-type runtime leaf") {
  HeadlessText text;
  Surface surface;
  State<bool> alternate{false};
  auto editor = Views::Component([&] {
                  return Views::Button("Open", [] {})
                      .Key(alternate.Get() ? "other" : "original");
                }).Key("editor");
  surface.SetContent(editor, text.Environment());
  REQUIRE(surface.Update({400, 200}, 1));
  const auto oldButton =
      surface.Tree().Get(surface.Tree().Root())->children.front();
  REQUIRE(surface.Focus(oldButton));
  REQUIRE(alternate.Set(true));
  REQUIRE(surface.Update({400, 200}, 1));
  CHECK_FALSE(surface.Tree().IsAlive(oldButton));
  CHECK_FALSE(surface.FocusedElement());
}
