#include <NGIN/UI/Backend/SDL3/Desktop.hpp>
#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/NativeText.hpp>
#include <NGIN/UI/Surface.hpp>

namespace NGIN::UI::SDL3 {
auto Open(WindowCreateInfo window, Views::View content,
          std::unique_ptr<IAccessibilityBackend> accessibility)
    -> UIResult<std::unique_ptr<Application>> {
  auto application = CreateApplication({
      .platform = CreatePlatformBackend(),
      .renderer = CreateRendererBackend(),
      .applicationName = window.title,
      .accessibility = std::move(accessibility),
  });
  if (!application) {
    return std::unexpected(application.error());
  }
  auto text = NativeTextSystem::Create((*application)->Renderer());
  if (!text) {
    return std::unexpected(text.error());
  }
  auto created = (*application)->CreateWindow(window);
  if (!created) {
    return std::unexpected(created.error());
  }
  auto &surface = (*created)->ContentSurface();
  (*text)->SetResourcesInvalidatedCallback(
      [invalidate = surface.Invalidator()] {
        invalidate(InvalidationKind::All);
      });
  surface.SetContent(std::move(content),
                    Views::ViewEnvironment{
                        std::shared_ptr<NativeTextSystem>(std::move(*text))});
  return application;
}

auto Run(WindowCreateInfo window, Views::View content,
         std::unique_ptr<IAccessibilityBackend> accessibility)
    -> UIResult<void> {
  auto application =
      Open(std::move(window), std::move(content), std::move(accessibility));
  if (!application) {
    return std::unexpected(application.error());
  }
  return (*application)->Run();
}
} // namespace NGIN::UI::SDL3
