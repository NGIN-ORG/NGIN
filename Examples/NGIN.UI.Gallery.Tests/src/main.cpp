#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

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

auto FindByTypeAndKey(const NGIN::UI::RuntimeTree &tree,
                      const NGIN::UI::ElementType type,
                      const std::string_view key) -> NGIN::UI::ElementHandle {
  std::vector<NGIN::UI::ElementHandle> pending{tree.Root()};
  while (!pending.empty()) {
    const auto handle = pending.back();
    pending.pop_back();
    const auto *node = tree.Get(handle);
    if (node == nullptr) {
      continue;
    }
    if (node->type == type && node->key.View() == key) {
      return handle;
    }
    pending.insert(pending.end(), node->children.begin(), node->children.end());
  }
  return {};
}

auto Center(const NGIN::UI::Rect bounds) noexcept -> NGIN::UI::Point {
  return {bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
}
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *platformObserver = platform.get();
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

  platformObserver->InjectEvent(
      WindowResized{window->PlatformHandle(), PixelSize{1180, 760}});
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(window->PixelExtent() == PixelSize{1180, 760},
             "resize updates the logical window") ||
      !Check(!rendererObserver->Surfaces().empty() &&
                 rendererObserver->Surfaces().front().size ==
                     PixelSize{1180, 760},
             "resize updates the render surface")) {
    return 1;
  }

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
    if (page == NGIN::UIGallery::Page::TextArea &&
        !Check(HasRole(window->Semantics(), SemanticRole::TextBox),
               "text-area page exposes editable text semantics")) {
      return 1;
    }
    if (page == NGIN::UIGallery::Page::Images &&
        !Check(HasRole(window->Semantics(), SemanticRole::Image),
               "images page exposes image semantics")) {
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

  model.SelectPage(NGIN::UIGallery::Page::Inputs);
  if (!application->PumpOnce()) {
    return 1;
  }
  const auto checkHandle =
      window->Tree().FindBySemanticIdentifier(NGIN::Text::String{
          "settings-check"});
  const auto *checkNode = window->Tree().Get(checkHandle);
  if (!Check(checkNode != nullptr, "checkbox has a stable semantic identity")) {
    return 1;
  }
  const auto checkCenter = Center(checkNode->arrangedBounds);
  const auto beforePointer = model.CheckBinding().Get();
  platformObserver->InjectEvent(PointerMoved{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .position = checkCenter,
  });
  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = checkCenter,
  });
  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .button = PointerButton::Primary,
      .state = ButtonState::Released,
      .position = checkCenter,
  });
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.CheckBinding().Get() != beforePointer,
             "pointer activation changes a gallery control")) {
    return 1;
  }

  const auto beforeKeyboard = model.CheckBinding().Get();
  const auto keyboardCheckHandle =
      window->Tree().FindBySemanticIdentifier(NGIN::Text::String{
          "settings-check"});
  if (!Check(window->FocusedElement() == keyboardCheckHandle ||
                 window->Focus(keyboardCheckHandle),
             "keyboard target accepts focus")) {
    return 1;
  }
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
      .state = KeyState::Pressed,
  });
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
      .state = KeyState::Released,
  });
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.CheckBinding().Get() != beforeKeyboard,
             "keyboard activation changes a gallery control")) {
    return 1;
  }

  const auto editor =
      FindByTypeAndKey(window->Tree(), ElementType::TextField, "editable");
  if (!Check(editor.IsValid() && window->Focus(editor),
             "editable field accepts focus")) {
    return 1;
  }
  if (!platformObserver
           ->SetClipboardText(NGIN::Text::String{" clipboard"})
           .HasValue()) {
    return 1;
  }
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>('V'),
      .state = KeyState::Pressed,
      .modifiers = static_cast<NGIN::UInt32>(KeyModifierFlags::Control),
  });
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.Name().View().find("clipboard") != std::string_view::npos,
             "clipboard paste commits through the focused binding")) {
    return 1;
  }
  const auto beforeComposition = model.Name();
  platformObserver->InjectEvent(TextComposition{
      .window = window->PlatformHandle(),
      .text = NGIN::Text::String{"\xC3\xA5"},
      .selectionStart = 0,
      .selectionLength = 2,
  });
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.Name() == beforeComposition,
             "IME pre-edit remains transient")) {
    return 1;
  }
  platformObserver->InjectEvent(TextInput{
      .window = window->PlatformHandle(),
      .text = NGIN::Text::String{"\xC3\xA5"},
  });
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(model.Name() != beforeComposition,
             "IME commit updates the focused binding")) {
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
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Escape),
      .state = KeyState::Pressed,
  });
  if (!application->PumpOnce() ||
      !Check(!model.IsPopupOpen(), "Escape dismisses the popup")) {
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

  const auto dialogRecord = std::find_if(
      platformObserver->Windows().begin(), platformObserver->Windows().end(),
      [](const TestWindowRecord &record) {
        return !record.destroyed && record.info.kind == WindowKind::Dialog;
      });
  if (!Check(dialogRecord != platformObserver->Windows().end(),
             "test platform records the modal dialog")) {
    return 1;
  }
  platformObserver->InjectEvent(WindowCloseRequested{dialogRecord->handle});
  if (!application->PumpOnce()) {
    return 1;
  }
  if (!Check(application->ActiveWindowCount() == 2,
             "dialog close updates multiple-window ownership") ||
      !Check(window->ActiveModalDialog() == nullptr,
             "dialog close restores the owner modal state")) {
    return 1;
  }

  std::cout << "NGIN.UI gallery headless checks passed\n";
  return 0;
}
