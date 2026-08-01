#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Version.hpp>

auto main() -> int {
  static_assert(NGIN::UI::VersionMajor == 0);
  static_assert(NGIN::UI::VersionMinor == 2);
  static_assert(NGIN::UI::VersionPatch == 0);
  NGIN::UI::Composer composer;
  composer.Leaf(NGIN::UI::ElementType::Border, "installed");
  return composer.Declarations().size() == 1 ? 0 : 1;
}
