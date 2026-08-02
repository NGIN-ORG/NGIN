#include <NGIN/UI/Hosting/Hosting.hpp>
#include <NGIN/UI/Version.hpp>

static_assert(NGIN::UI::VersionMajor == 0);
static_assert(NGIN::UI::VersionMinor == 4);

auto main() -> int {
  NGIN::UI::PageRegistry pages;
  return pages.Pages().empty() ? 0 : 1;
}
