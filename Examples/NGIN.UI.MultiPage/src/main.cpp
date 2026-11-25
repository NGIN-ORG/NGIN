#include <NGIN/Reflection/ModuleInit.hpp>
#include <NGIN/Reflection/TypeBuilder.hpp>
#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/Components.hpp>
#include <NGIN/UI/Hosting/Hosting.hpp>
#include <NGIN/Units.hpp>
#if defined(NGIN_PLATFORM_WINDOWS)
#include <NGIN/UI/Accessibility/Windows/Windows.hpp>
#endif

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace MultiPage {
using namespace NGIN;
using namespace NGIN::UI;

struct HomePage final {};
struct DetailPage final {};

struct DetailParameter final {
  UInt32 itemId{0};
};

struct NavigationActions final {
  NGIN::Utilities::Callable<void(UInt32)> openDetail{};
  NGIN::Utilities::Callable<void()> back{};
};

class MessageService final {
public:
  [[nodiscard]] auto Load() const -> Text::String {
    return Text::String{"Item details are ready."};
  }
};

class HomeViewModel final {
public:
  using Dependencies = Core::ServiceDependencies<NavigationActions>;

  explicit HomeViewModel(Memory::Shared<NavigationActions> actions) noexcept
      : m_actions(std::move(actions)) {}

  void OpenDetail() { m_actions->openDetail(42); }

private:
  Memory::Shared<NavigationActions> m_actions{};
};

class DetailViewModel final {
public:
  DetailViewModel(Memory::Shared<MessageService> messages,
                  Memory::Shared<NavigationActions> actions)
      : m_messages(std::move(messages)), m_actions(std::move(actions)) {}

  [[nodiscard]] auto ActivateAsync(NGIN::Async::TaskContext &context)
      -> ViewModelTaskScope::Task {
    static_cast<void>(m_status->Set(Text::String{"Loading..."}));
    co_await ViewModelTaskScope::Task::Delay(context,
                                             NGIN::Units::Milliseconds{700.0});
    static_cast<void>(m_status->Set(m_messages->Load()));
  }

  void Back() { m_actions->back(); }
  [[nodiscard]] auto Status() const noexcept -> const Text::String & {
    return m_status->Get();
  }

private:
  Memory::Shared<MessageService> m_messages{};
  Memory::Shared<NavigationActions> m_actions{};
  std::unique_ptr<State<Text::String>> m_status =
      std::make_unique<State<Text::String>>(Text::String{"Waiting to load"});
};

inline void NginReflect(Reflection::Tag<DetailViewModel>,
                        Reflection::TypeBuilder<DetailViewModel> &type) {
  type.InjectableConstructor<Memory::Shared<MessageService>,
                             Memory::Shared<NavigationActions>>();
}

auto PageProperties() -> NodeProperties {
  NodeProperties page{};
  page.layout.padding = Thickness::Uniform(Dp{28.0F});
  page.layout.gap = 18.0F;
  return page;
}

auto Heading(Text::String title) -> Views::View {
  NodeProperties heading{};
  heading.text.fontSize = 30.0F;
  heading.semantics.role = SemanticRole::Heading;
  return Views::Label(std::move(title), heading).Key("title");
}

auto HomeView(HomeViewModel &viewModel, const NoNavigationParameter &)
    -> Views::View {
  return Views::Column(
             {Heading(Text::String{"Multi-page app"}),
              Views::Label(
                  "Services build the ViewModels. Views build the controls."),
              Views::Button(Text::String{"Open item 42"},
                            [&viewModel] { viewModel.OpenDetail(); })
                  .Key("open-detail")},
             PageProperties())
      .Key("home");
}

auto DetailView(DetailViewModel &viewModel, const DetailParameter &parameter)
    -> Views::View {
  return Views::Column(
             {Heading(Text::String{"Item " + std::to_string(parameter.itemId)}),
              Views::Component([&viewModel] {
                return Views::Label(viewModel.Status());
              }).Key("status"),
              Views::Button(Text::String{"Back"},
                            [&viewModel] { viewModel.Back(); })
                  .Key("back")},
             PageProperties())
      .Key("detail");
}
} // namespace MultiPage

namespace {
auto Report(const char *context, const char *message) -> int {
  std::cerr << context << ": " << message << '\n';
  return 1;
}

class MultiPagePresentationModule final : public NGIN::Core::IModule {
public:
  MultiPagePresentationModule(NGIN::UI::Hosting::UIHostingRegistration hosting,
                              std::shared_ptr<NGIN::UI::PageRegistry> pages,
                              const bool smoke) noexcept
      : m_hosting(std::move(hosting)), m_pages(std::move(pages)),
        m_smoke(smoke) {}

  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    using namespace NGIN::UI;
    using namespace NGIN::UI::Hosting;

    auto actions =
        context.Services().ResolveRequired<MultiPage::NavigationActions>();
    if (!actions) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          actions.error());
    }
    m_actions = std::move(actions).value();
    auto window = m_hosting.services->CreateWindow(
        {.id = NGIN::Text::String{"MultiPage.Main"},
         .title = NGIN::Text::String{"NGIN.UI Multi-Page"},
         .initialSize = PixelSize{760, 480},
         .minimumSize = PixelSize{520, 360}});
    if (!window) {
      return Failure("Window creation", window.error().message.c_str());
    }
    m_window = std::move(window).value();

    auto activation = CreateHostedNavigationContext(m_hosting, m_window);
    if (!activation) {
      return Failure("Navigation context", activation.error().message.CStr());
    }
    m_activation.emplace(std::move(activation).value());
    m_navigation = std::make_unique<NavigationService>(
        *m_pages, *m_activation,
        NavigationOptions{.region = "Main",
                          .cacheCapacity = 1,
                          .isOnScheduler = [dispatcher = m_hosting.dispatcher] {
                            return dispatcher->IsCurrentThread();
                          }});
    m_content = std::make_unique<NavigationHost>(*m_navigation);
    m_actions->openDetail = [this](const NGIN::UInt32 itemId) {
      static_cast<void>(m_navigation->Navigate<MultiPage::DetailPage>(
          MultiPage::DetailParameter{itemId}));
    };
    m_actions->back = [this] { static_cast<void>(m_navigation->Back()); };
    m_window.UI()->SetContent(
        m_content->ContentView(),
        Views::ViewEnvironment{m_hosting.runtime->Text()});
    m_window.UI()->SetEventHandler([this](const PlatformEvent &event) {
      static_cast<void>(m_content->HandleEvent(event));
    });
    auto posted = m_hosting.dispatcher->Post([this] {
      if (!m_navigation->Start<MultiPage::HomePage>()) {
        m_hosting.runtime->UI().RequestExit();
        return;
      }
      if (m_smoke) {
        if (!m_navigation->Navigate<MultiPage::DetailPage>(
                MultiPage::DetailParameter{42})) {
          m_hosting.runtime->UI().RequestExit();
          return;
        }
        static_cast<void>(m_hosting.dispatcher->Post(
            [this] { m_hosting.runtime->UI().RequestExit(); }));
      }
    });
    if (!posted) {
      return Failure("Initial navigation", "could not schedule the first page");
    }
    return {};
  }

  auto OnStop(NGIN::Core::ModuleContext &) noexcept
      -> NGIN::Core::CoreResult<void> override {
    if (m_actions) {
      m_actions->openDetail = {};
      m_actions->back = {};
    }
    if (m_window.IsOpen()) {
      m_window.UI()->SetContent(
          {}, NGIN::UI::Views::ViewEnvironment{m_hosting.runtime->Text()});
      m_window.UI()->SetEventHandler({});
    }
    m_content.reset();
    if (m_navigation) {
      static_cast<void>(m_navigation->Clear());
      m_navigation.reset();
    }
    m_activation.reset();
    return {};
  }

private:
  static auto Failure(const char *operation, const char *message)
      -> NGIN::Core::CoreResult<void> {
    return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
        NGIN::Core::MakeKernelError(NGIN::Core::KernelErrorCode::InternalError,
                                    "NGIN.UI.MultiPage", operation, message));
  }

  NGIN::UI::Hosting::UIHostingRegistration m_hosting{};
  std::shared_ptr<NGIN::UI::PageRegistry> m_pages{};
  NGIN::Memory::Shared<MultiPage::NavigationActions> m_actions{};
  bool m_smoke{false};
  NGIN::UI::Hosting::HostedWindow m_window{};
  std::optional<NGIN::UI::Hosting::HostedNavigationContext> m_activation{};
  std::unique_ptr<NGIN::UI::NavigationService> m_navigation{};
  std::unique_ptr<NGIN::UI::NavigationHost> m_content{};
};
} // namespace

auto main(const int argc, char **argv) -> int {
  using namespace NGIN;
  using namespace NGIN::Core;
  using namespace NGIN::UI;
  using namespace NGIN::UI::Hosting;
  using namespace MultiPage;

  bool smoke = false;
  for (int index = 1; index < argc; ++index) {
    if (std::string_view{argv[index]} == "--smoke") {
      smoke = true;
      continue;
    }
  }

  Reflection::ModuleRegistration reflected{"NGIN.UI.MultiPage.Reflection"};
  reflected.RegisterType<DetailViewModel>();
  if (!reflected.Commit()) {
    return Report("Reflection registration failed", "DetailViewModel");
  }

  auto builder = CreateApplicationBuilder(argc, argv);
  ApplicationCreateInfo application{.platform = SDL3::CreatePlatformBackend(),
                                    .renderer = SDL3::CreateRendererBackend(),
                                    .applicationName =
                                        Text::String{"NGIN.UI Multi-Page"}};
#if defined(NGIN_PLATFORM_WINDOWS)
  application.accessibility =
      Accessibility::Windows::CreateAccessibilityBackend();
#endif
  auto hosting =
      ConfigureUIHosting(*builder, {.application = std::move(application)});
  if (!hosting) {
    return Report("UI hosting failed", hosting.error().message.CStr());
  }

  auto pages = std::make_shared<PageRegistry>();
  auto pageBuilder = ConfigureUIPages(*builder, *pages);
  pageBuilder.Services()
      .AddSingleton<NavigationActions>()
      .AddScoped<MessageService>();
  auto home = pageBuilder.AddPage<HomePage, HomeViewModel>(
      {.id = "home", .displayName = "Home", .routeName = "home"}, HomeView);
  auto detail =
      pageBuilder.AddPage<DetailPage, DetailViewModel, DetailParameter>(
          {.id = "detail", .displayName = "Detail", .routeName = "item"},
          DetailView);
  if (!home || !detail) {
    return Report("Page registration failed", "invalid page catalogue");
  }

  ModuleOptions presentation{};
  presentation.family = ModuleFamily::App;
  presentation.startupStage = StartupStage::Presentation;
  presentation.requiresServices = {
      UIApplicationServiceName,
      UIDispatcherServiceName,
      UIServiceProviderServiceName,
  };
  auto app =
      builder->SetApplicationName("NGIN.UI.MultiPage")
          .AddDefaultServices()
          .AddConfiguration()
          .AddModule(
              "NGIN.UI.MultiPage.Presentation", std::move(presentation),
              [hosting = hosting.value(), pages,
               smoke]() mutable -> CoreResult<Memory::Shared<IModule>> {
                try {
                  return Memory::MakeSharedAs<IModule,
                                              MultiPagePresentationModule>(
                      std::move(hosting), std::move(pages), smoke);
                } catch (...) {
                  return Utilities::Unexpected<KernelError>(MakeKernelError(
                      KernelErrorCode::ModuleFactoryFailure,
                      "NGIN.UI.MultiPage", "NGIN.UI.MultiPage.Presentation",
                      "presentation module allocation failed"));
                }
              })
          .Build();
  if (!app) {
    return Report("Application build failed", app.error().message.c_str());
  }
  auto run = app.value()->Run();
  return run ? 0
             : Report("Application run failed", run.error().message.c_str());
}
