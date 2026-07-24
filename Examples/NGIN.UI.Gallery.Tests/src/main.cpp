#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

namespace {
auto Check(const bool condition, const char *message) -> bool {
  if (!condition) {
    std::cerr << "Gallery test failed: " << message << '\n';
  }
  return condition;
}

auto Report(const char *context, const NGIN::UI::UIError &error) -> int {
  std::cerr << context << ": " << error.message.CStr() << '\n';
  return 1;
}

auto HasRole(const NGIN::UI::SemanticTree &tree,
             const NGIN::UI::SemanticRole role) -> bool {
  return std::any_of(
      tree.Nodes().begin(), tree.Nodes().end(),
      [role](const NGIN::UI::SemanticNode &node) { return node.role == role; });
}
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *rendererObserver = renderer.get();
  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::move(renderer),
      .applicationName = NGIN::Text::String{"NGIN.UI Gallery Tests"},
      .enableRendererValidation = true,
  });
  if (!createdApplication) {
    return Report("Application creation failed", createdApplication.Error());
  }
  auto application = std::move(createdApplication).Value();

  auto createdText = NativeTextSystem::Create(application->Renderer());
  if (!createdText) {
    return Report("Native text creation failed", createdText.Error());
  }
  auto text = std::move(createdText).Value();

  NGIN::UIGallery::Model model;
  auto createdWindow =
      NGIN::UIGallery::CreateMainWindow(*application, *text, model);
  if (!createdWindow) {
    return Report("Gallery window creation failed", createdWindow.Error());
  }
  auto *window = createdWindow.Value();

  for (NGIN::UIntSize index = 0; index < NGIN::UIGallery::PageCount; ++index) {
    const auto page = NGIN::UIGallery::PageAt(index);
    model.SelectPage(page);
    auto pumped = application->PumpOnce();
    if (!pumped) {
      return Report("Gallery page frame failed", pumped.Error());
    }
    if (!Check(model.CurrentPage() == page,
               "page selection is deterministic") ||
        !Check(!NGIN::UIGallery::PageName(page).empty(),
               "every page has a name") ||
        !Check(!window->Semantics().Nodes().empty(),
               "every page emits semantics")) {
      return 1;
    }
    if (page == NGIN::UIGallery::Page::Inputs &&
        (!Check(HasRole(window->Semantics(), SemanticRole::CheckBox),
                "inputs page exposes checkbox semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::RadioButton),
                "inputs page exposes radio semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::Switch),
                "inputs page exposes switch semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::Slider),
                "inputs page exposes slider semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::ProgressBar),
                "inputs page exposes progress semantics"))) {
      return 1;
    }
    if (page == NGIN::UIGallery::Page::Collections &&
        (!Check(HasRole(window->Semantics(), SemanticRole::List),
                "collections page exposes list semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::ListItem),
                "collections page exposes list-item semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::ComboBox),
                "collections page exposes combo-box semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::TabList),
                "collections page exposes tab-list semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::Tab),
                "collections page exposes tab semantics") ||
         !Check(HasRole(window->Semantics(), SemanticRole::TabPanel),
                "collections page exposes active tab-panel semantics"))) {
      return 1;
    }
  }

  if (!Check(rendererObserver->RenderPackets().size() >=
                 NGIN::UIGallery::PageCount,
             "every gallery page rendered")) {
    return 1;
  }

  const auto wasLight = model.IsLightTheme();
  model.ToggleTheme();
  model.Activate();
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.IsLightTheme() != wasLight, "theme switching is stateful") ||
      !Check(model.ActivationCount() == 1,
             "control activation state is retained")) {
    return 1;
  }

  auto checkChanged = model.CheckBinding().Set(CheckState::Indeterminate);
  auto toggleChanged = model.ToggleBinding().Set(false);
  auto sliderChanged = model.SliderBinding().Set(0.8F);
  auto radioChanged =
      model.DensityBinding().Set(NGIN::UIGallery::Density::Spacious);
  if (!Check(checkChanged.HasValue(), "checkbox binding is writable") ||
      !Check(toggleChanged.HasValue(), "toggle binding is writable") ||
      !Check(sliderChanged.HasValue(), "slider binding is writable") ||
      !Check(radioChanged.HasValue(), "typed radio binding is writable") ||
      !Check(model.SliderValue() == 0.8F,
             "range state is retained by the gallery model")) {
    return 1;
  }

  model.SelectPage(NGIN::UIGallery::Page::Overlays);
  model.SetPopupOpen(true);
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.IsPopupOpen(), "popup state is controllable") ||
      !Check(window->Diagnostics().semanticNodeCount > 0,
             "popup frame updates diagnostics")) {
    return 1;
  }

  model.ToggleInspector();
  if (!Check(model.IsInspectorEnabled(), "inspector state is retained") ||
      !Check(window->InspectorOverlay().enabled,
             "inspector overlay follows the model")) {
    return 1;
  }

  auto openedWindow = model.OpenAuxiliaryWindow(false);
  if (!openedWindow) {
    return Report("Auxiliary window creation failed", openedWindow.Error());
  }
  auto openedDialog = model.OpenAuxiliaryWindow(true);
  if (!openedDialog) {
    return Report("Dialog creation failed", openedDialog.Error());
  }
  auto pumpedWindows = application->PumpOnce();
  if (!pumpedWindows) {
    return Report("Multiple-window frame failed", pumpedWindows.Error());
  }
  if (!Check(application->ActiveWindowCount() == 3,
             "gallery creates independent and modal windows") ||
      !Check(window->ActiveModalDialog() != nullptr,
             "modal ownership is established")) {
    return 1;
  }

  std::cout << "NGIN.UI gallery headless checks passed\n";
  return 0;
}
