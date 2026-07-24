#include <NGIN/UI/Composer.hpp>

auto main() -> int {
  NGIN::UI::Composer composer;
  composer.Leaf(NGIN::UI::ElementType::Border, "installed");
  return composer.Declarations().size() == 1 ? 0 : 1;
}
