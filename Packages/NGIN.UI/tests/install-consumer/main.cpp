#include "../HeadlessTextFixture.hpp"
#include <NGIN/UI/UI.hpp>

#if __has_include(<NGIN/UI/Composer.hpp>)
#error The installed package exposes its internal declaration builder.
#endif

template <typename T>
concept AcceptsRawComposition =
    requires(T &surface) { surface.SetComposition({}); };
static_assert(!AcceptsRawComposition<NGIN::UI::Surface>);

auto main() -> int {
  using namespace NGIN::UI;

  static_assert(VersionMajor == 0);
  static_assert(VersionMinor == 4);
  static_assert(VersionPatch == 0);

  State<int> count{0};
  Testing::HeadlessText text;
  {
    Surface surface;
    surface.SetContent(Views::Component([&] {
                         NodeProperties label{};
                         label.semantics.identifier = "count";
                         return Views::Label(
                             count.Get() ? "Updated" : "Initial", label);
                       }),
                       text.Environment());
    auto frame = surface.Update({160, 90}, 1.0F);
    if (!frame || (*frame)->loadAction != RenderLoadAction::Preserve) {
      return 1;
    }
    const auto label = surface.Tree().FindBySemanticIdentifier("count");
    if (!label ||
        surface.Tree().Get(label)->properties.text.value != "Initial") {
      return 1;
    }
    Command increment{[&] { static_cast<void>(count.Set(count.Get() + 1)); }};
    if (increment.Execute() != CommandInvocation::Started || count.Get() != 1 ||
        !surface.IsDirty() || !surface.Update({160, 90}, 1.0F)) {
      return 1;
    }
    if (!surface.Tree().IsAlive(label) ||
        surface.Tree().Get(label)->properties.text.value != "Updated") {
      return 1;
    }
  }
  return CurrentSubscriptionDiagnostics().activeCount == 0 ? 0 : 1;
}
