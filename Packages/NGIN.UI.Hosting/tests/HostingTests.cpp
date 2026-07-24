#include <NGIN/UI/Hosting/Hosting.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <iostream>
#include <memory>

namespace {
auto Fail(const char *message) -> int {
  std::cerr << message << '\n';
  return 1;
}

class PresentationModule final : public NGIN::Core::IModule {
public:
  explicit PresentationModule(bool &started) noexcept : m_started(&started) {}

  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    using namespace NGIN::UI;
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

    auto window = runtime.Value()->UI().CreateWindow(WindowCreateInfo{
        .id = NGIN::Text::String{"Hosted.Tests"},
        .title = NGIN::Text::String{"Hosted tests"},
        .initialSize = PixelSize{160, 90},
    });
    if (!window) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          NGIN::Core::MakeKernelError(
              NGIN::Core::KernelErrorCode::InternalError,
              "NGIN.UI.Hosting.Tests", "CreateWindow",
              window.Error().message.CStr()));
    }
    window.Value()->SetContent([](Composer &composer) {
      NodeProperties properties{};
      properties.layout.preferredSize = Size{80.0F, 40.0F};
      properties.paintsBackground = true;
      properties.background = Color{0.2F, 0.5F, 0.8F, 1.0F};
      composer.Leaf(ElementType::Rectangle, properties, "content");
    });

    auto runtimeService = runtime.Value();
    auto posted = dispatcher.Value()->Post(
        [runtimeService] { runtimeService->UI().RequestExit(); });
    if (!posted) {
      return posted;
    }
    *m_started = true;
    return {};
  }

private:
  bool *m_started{nullptr};
};
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Hosting;

  auto renderer = std::make_unique<Testing::RecordingRenderBackend>();
  auto *rendererObserver = renderer.get();

  auto builder = NGIN::Core::CreateApplicationBuilder(0, nullptr);
  auto hosting = ConfigureUIHosting(
      *builder,
      UIHostingCreateInfo{
          .application =
              ApplicationCreateInfo{
                  .platform = std::make_unique<Testing::TestPlatformBackend>(),
                  .renderer = std::move(renderer),
                  .applicationName = NGIN::Text::String{"Hosting tests"},
              },
          .maximumWait = std::chrono::milliseconds{10},
      });
  if (!hosting) {
    return Fail("Failed to configure UI hosting");
  }

  bool presentationStarted = false;
  NGIN::Core::ModuleOptions options{};
  options.family = NGIN::Core::ModuleFamily::App;
  options.startupStage = NGIN::Core::StartupStage::Presentation;
  options.requiresServices = {
      UIApplicationServiceName,
      UIDispatcherServiceName,
  };
  builder->SetApplicationName("NGIN.UI.Hosting.Tests")
      .AddModule("NGIN.UI.Hosting.Tests.Presentation", std::move(options),
                 [&presentationStarted]()
                     -> NGIN::Core::CoreResult<
                         NGIN::Memory::Shared<NGIN::Core::IModule>> {
                   return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                                     PresentationModule>(
                       presentationStarted);
                 });

  auto built = builder->Build();
  if (!built) {
    return Fail("Failed to build hosted application");
  }
  auto ran = built.Value()->Run();
  if (!ran) {
    return Fail("Hosted UI run loop failed");
  }
  if (!presentationStarted) {
    return Fail("Presentation module did not resolve hosted UI services");
  }
  if (rendererObserver->Surfaces().size() != 1 ||
      rendererObserver->Surfaces().front().renderCount != 1 ||
      rendererObserver->Surfaces().front().presentCount != 1) {
    return Fail("Hosted UI did not render and present its first frame");
  }
  if (rendererObserver->WaitIdleCount() != 1) {
    return Fail("Hosted UI module did not wait for renderer shutdown");
  }
  return 0;
}
