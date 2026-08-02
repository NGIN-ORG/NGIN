#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/UI/Version.hpp>
#include <NGIN/UI/ViewModel.hpp>

auto main() -> int {
  static_assert(NGIN::UI::VersionMajor == 0);
  static_assert(NGIN::UI::VersionMinor == 3);
  static_assert(NGIN::UI::VersionPatch == 0);
  NGIN::UI::State<int> count{0};
  NGIN::UI::Command increment{
      [&count] { static_cast<void>(count.Set(count.Get() + 1)); }};
  const auto invoked = increment.Execute();
  if (invoked != NGIN::UI::CommandInvocation::Started || count.Get() != 1) {
    return 1;
  }
  if (NGIN::UI::CurrentSubscriptionDiagnostics().activeCount != 0) {
    return 1;
  }
  NGIN::UI::Composer composer;
  composer.Leaf(NGIN::UI::ElementType::Border, "installed");
  return composer.Declarations().size() == 1 ? 0 : 1;
}
