#include <NGIN/UI/Accessibility/Windows/Windows.hpp>
#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/Hosting/Hosting.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

namespace {
auto ReportUIError(const char *context, const NGIN::UI::UIError &error) -> int {
  std::cerr << context << ": " << error.message.CStr() << '\n';
  return 1;
}

auto ReportCoreError(const char *context, const NGIN::Core::KernelError &error)
    -> int {
  std::cerr << context << ": " << error.message << '\n';
  return 1;
}

class GalleryPresentationModule final : public NGIN::Core::IModule {
public:
  GalleryPresentationModule(
      const bool smoke,
      const std::optional<NGIN::UIGallery::Page> initialPage) noexcept
      : m_smoke(smoke), m_initialPage(initialPage) {}

  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    using namespace NGIN::UI::Hosting;

    auto runtime = context.Services().ResolveRequired<HostedUIRuntime>(
        UIApplicationServiceName);
    if (!runtime) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          runtime.Error());
    }
    auto dispatcher = context.Services().ResolveRequired<IUIDispatcher>(
        UIDispatcherServiceName);
    if (!dispatcher) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          dispatcher.Error());
    }
    m_runtime = runtime.Value();
    m_dispatcher = dispatcher.Value();

    if (m_initialPage) {
      m_model.SelectPage(*m_initialPage);
    }
    auto window = NGIN::UIGallery::CreateMainWindow(m_runtime->UI(),
                                                    m_runtime->Text(), m_model);
    if (!window) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          NGIN::Core::MakeKernelError(
              NGIN::Core::KernelErrorCode::InternalError,
              "NGIN.UI.Gallery.Hosted", "CreateMainWindow",
              window.Error().message.CStr()));
    }
    if (m_smoke) {
      return m_dispatcher->Post([this] { AdvanceSmoke(); });
    }
    return {};
  }

private:
  void AdvanceSmoke() noexcept {
    if (m_smokePage >= NGIN::UIGallery::PageCount) {
      m_runtime->UI().RequestExit();
      return;
    }
    m_model.SelectPage(NGIN::UIGallery::PageAt(m_smokePage));
    ++m_smokePage;
    auto posted = m_dispatcher->Post([this] { AdvanceSmoke(); });
    if (!posted) {
      m_runtime->UI().RequestExit();
    }
  }

  bool m_smoke{false};
  std::optional<NGIN::UIGallery::Page> m_initialPage{};
  NGIN::UIntSize m_smokePage{0};
  NGIN::UIGallery::GalleryViewModel m_model{};
  NGIN::Memory::Shared<NGIN::UI::Hosting::HostedUIRuntime> m_runtime{};
  NGIN::Memory::Shared<NGIN::UI::Hosting::IUIDispatcher> m_dispatcher{};
};
} // namespace

auto main(const int argc, char **argv) -> int {
  using namespace NGIN::Core;
  using namespace NGIN::UI;
  using namespace NGIN::UI::Hosting;

  bool smoke = false;
  std::optional<NGIN::UIGallery::Page> initialPage;
  for (int index = 1; index < argc; ++index) {
    const auto argument = std::string_view{argv[index]};
    if (argument == "--smoke") {
      smoke = true;
      continue;
    }
    if (argument == "--page" && index + 1 < argc) {
      const auto requested = std::string_view{argv[++index]};
      for (NGIN::UIntSize page = 0; page < NGIN::UIGallery::PageCount; ++page) {
        const auto candidate = NGIN::UIGallery::PageAt(page);
        if (NGIN::UIGallery::PageName(candidate) == requested) {
          initialPage = candidate;
          break;
        }
      }
      if (!initialPage) {
        std::cerr << "Unknown gallery page: " << requested << '\n';
        return 2;
      }
      continue;
    }
    std::cerr << "Usage: NGIN.UI.Gallery.Hosted [--smoke] [--page <name>]\n";
    return 2;
  }

  auto builder = CreateApplicationBuilder(argc, argv);
  auto hosting = ConfigureUIHosting(
      *builder,
      UIHostingCreateInfo{
          .application =
              ApplicationCreateInfo{
                  .platform = SDL3::CreatePlatformBackend(),
                  .renderer = SDL3::CreateRendererBackend(),
                  .applicationName =
                      NGIN::Text::String{"NGIN.UI Gallery Hosted"},
                  .enableRendererValidation = true,
                  .accessibility =
                      Accessibility::Windows::CreateAccessibilityBackend(),
              },
      });
  if (!hosting) {
    return ReportUIError("UI hosting configuration failed", hosting.Error());
  }

  ModuleOptions presentation{};
  presentation.family = ModuleFamily::App;
  presentation.startupStage = StartupStage::Presentation;
  presentation.requiresServices = {
      UIApplicationServiceName,
      UIDispatcherServiceName,
  };
  builder->SetApplicationName("NGIN.UI.Gallery.Hosted")
      .AddDefaultServices()
      .AddConfiguration()
      .AddModule(
          "NGIN.UI.Gallery.Presentation", std::move(presentation),
          [smoke, initialPage]() -> CoreResult<NGIN::Memory::Shared<IModule>> {
            try {
              return NGIN::Memory::MakeSharedAs<IModule,
                                                GalleryPresentationModule>(
                  smoke, initialPage);
            } catch (...) {
              return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                  KernelErrorCode::ModuleFactoryFailure,
                  "NGIN.UI.Gallery.Hosted", "NGIN.UI.Gallery.Presentation",
                  "presentation module allocation failed"));
            }
          });

  auto built = builder->Build();
  if (!built) {
    return ReportCoreError("Hosted application build failed", built.Error());
  }
  auto run = built.Value()->Run();
  return run ? 0
             : ReportCoreError("Hosted application run failed", run.Error());
}
