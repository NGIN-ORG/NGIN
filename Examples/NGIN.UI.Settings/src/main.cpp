#include "GameHost.hpp"
#include "SettingsPanel.hpp"

#include <NGIN/UI/Backend/SDL3/Desktop.hpp>
#if defined(NGIN_PLATFORM_WINDOWS)
#include <NGIN/UI/Accessibility/Windows/Windows.hpp>
#endif

#include <iostream>
#include <string_view>

namespace {
auto RunDesktop(bool smoke) -> NGIN::UI::UIResult<void> {
  using namespace NGIN::UI;
  auto application = SDL3::Open(
      {.id = "settings", .title = "Audio settings", .initialSize = {800, 480}},
      SettingsDemo::SettingsPanel()
#if defined(NGIN_PLATFORM_WINDOWS)
          ,
      Accessibility::Windows::CreateAccessibilityBackend()
#endif
  );
  if (!application) {
    return std::unexpected(application.error());
  }
  if (smoke) {
    if (auto frame = (*application)->PumpOnce(); !frame) {
      return frame;
    }
    return (*application)->ShutdownTasks();
  }
  return (*application)->Run();
}
} // namespace

int main(int argc, char **argv) {
  bool game = false;
  bool smoke = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--game") {
      game = true;
    } else if (arg == "--smoke") {
      smoke = true;
    } else {
      std::cerr << "Usage: NGIN.UI.Settings [--game] [--smoke]\n";
      return 2;
    }
  }
  auto result = game ? SettingsDemo::RunGame(smoke) : RunDesktop(smoke);
  if (!result) {
    std::cerr << result.error().operation.CStr() << ' '
              << result.error().message.CStr() << '\n';
    return 1;
  }
}
