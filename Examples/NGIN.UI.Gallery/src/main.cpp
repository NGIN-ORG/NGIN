#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <iostream>
#include <string_view>
#include <utility>

namespace {
auto ReportError(const char *context, const NGIN::UI::UIError &error) -> int {
  std::cerr << context << ": ";
  if (!error.backend.Empty()) {
    std::cerr << error.backend.CStr() << '/';
  }
  if (!error.operation.Empty()) {
    std::cerr << error.operation.CStr() << ": ";
  }
  std::cerr << error.message.CStr() << '\n';
  return 1;
}
} // namespace

auto main(const int argc, char **argv) -> int {
  using namespace NGIN::UI;

  const bool smoke =
      argc > 1 && std::string_view{argv[1]} == std::string_view{"--smoke"};

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = SDL3::CreatePlatformBackend(),
      .renderer = SDL3::CreateRendererBackend(),
      .applicationName = NGIN::Text::String{"NGIN.UI Gallery"},
      .enableRendererValidation = true,
  });
  if (!createdApplication) {
    return ReportError("Application creation failed",
                       createdApplication.Error());
  }
  auto application = std::move(createdApplication).Value();

  auto createdText = NativeTextSystem::Create(application->Renderer());
  if (!createdText) {
    return ReportError("Native text creation failed", createdText.Error());
  }
  auto text = std::move(createdText).Value();

  NGIN::UIGallery::Model model;
  auto window = NGIN::UIGallery::CreateMainWindow(*application, *text, model);
  if (!window) {
    return ReportError("Window creation failed", window.Error());
  }

  if (smoke) {
    for (int frame = 0; frame < 3; ++frame) {
      auto pumped = application->PumpOnce();
      if (!pumped) {
        return ReportError("Native smoke frame failed", pumped.Error());
      }
    }
    return 0;
  }

  auto run = application->Run();
  return run ? 0 : ReportError("Application run failed", run.Error());
}
