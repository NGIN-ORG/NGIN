#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/UI.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {
using NGIN::F32;
using NGIN::Text::String;
using namespace NGIN::UI;

auto ReportError(const char *context, const UIError &error) -> int {
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

auto TextProperties(NativeTextSystem &text, const F32 fontSize,
                    const Color color) -> NodeProperties {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = fontSize;
  properties.text.color = color;
  properties.text.geometry = &text;
  properties.text.wrapping = TextWrapping::NoWrap;
  return properties;
}

void ComposeText(Composer &composer, NativeTextSystem &text, String value,
                 const F32 fontSize, const Color color,
                 const std::string_view key) {
  composer.Text(std::move(value), text, text,
                TextProperties(text, fontSize, color), key);
}

void ComposeButton(Composer &composer, NativeTextSystem &text,
                   const char *label,
                   NGIN::Utilities::Callable<void()> onActivate,
                   const std::string_view key) {
  NodeProperties button{};
  button.layout.preferredSize = Size{210.0F, 48.0F};
  button.layout.padding = Thickness{16.0F, 10.0F, 16.0F, 10.0F};
  button.layout.horizontalAlignment = HorizontalAlignment::Start;
  button.layout.verticalAlignment = VerticalAlignment::Start;
  button.interaction.focusable = true;
  button.interaction.onActivate = std::move(onActivate);
  button.semantics.role = SemanticRole::Button;
  button.semantics.label = String{label};
  button.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Focus;
  button.paintsBackground = true;
  button.background = Color{0.12F, 0.42F, 0.78F, 1.0F};

  auto scope = composer.Begin(ElementType::Button, button, key);
  auto labelProperties =
      TextProperties(text, 16.0F, Color{1.0F, 1.0F, 1.0F, 1.0F});
  labelProperties.layout.horizontalAlignment = HorizontalAlignment::Center;
  labelProperties.layout.verticalAlignment = VerticalAlignment::Center;
  composer.Text(String{label}, text, text, labelProperties, "label");
}

auto CounterText(const std::uint32_t count) -> String {
  String result{"Button activations: "};
  const auto number = std::to_string(count);
  result.Append(number.c_str());
  result.Append(" (click or press Enter/Space)");
  return result;
}
} // namespace

auto main(const int argc, char **argv) -> int {
  using namespace NGIN::UI;

  const bool smoke =
      argc > 1 && std::string_view{argv[1]} == std::string_view{"--smoke"};

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = SDL3::CreatePlatformBackend(),
      .renderer = SDL3::CreateRendererBackend(),
      .applicationName = String{"NGIN.UI Gallery"},
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

  Window *mainWindow = nullptr;
  State<String> name{
      String{"NGIN"},
      [&](const InvalidationKind kind) {
        if (mainWindow != nullptr) {
          mainWindow->Invalidate(kind);
        }
      },
  };
  std::uint32_t activationCount = 0;

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = String{"Gallery.Main"},
      .title = String{"NGIN.UI Gallery"},
      .initialSize = PixelSize{900, 640},
      .minimumSize = PixelSize{640, 480},
  });
  if (!createdWindow) {
    return ReportError("Window creation failed", createdWindow.Error());
  }
  mainWindow = createdWindow.Value();

  mainWindow->SetContent([&](Composer &composer) {
    NodeProperties root{};
    root.layout.padding = Thickness{36.0F, 30.0F, 36.0F, 30.0F};
    root.layout.gap = 16.0F;
    root.paintsBackground = true;
    root.background = Color{0.035F, 0.055F, 0.09F, 1.0F};
    root.semantics.role = SemanticRole::Group;
    root.semantics.label = String{"NGIN.UI native gallery"};

    composer.Element(
        ElementType::Column, root,
        [&] {
          ComposeText(composer, *text, String{"NGIN.UI"}, 34.0F,
                      Color{0.55F, 0.78F, 1.0F, 1.0F}, "title");
          ComposeText(
              composer, *text,
              String{"Retained UI \xC2\xB7 HarfBuzz + FreeType \xC2\xB7 "
                     "SDL3 + SDL_GPU"},
              17.0F, Color{0.72F, 0.78F, 0.88F, 1.0F}, "subtitle");
          ComposeText(composer, *text,
                      String{"Type your name (UTF-8 and IME supported):"},
                      15.0F, Color{0.88F, 0.91F, 0.96F, 1.0F}, "name-label");

          NodeProperties field{};
          field.layout.preferredSize = Size{520.0F, 50.0F};
          field.layout.maximumSize.width = 620.0F;
          field.layout.padding = Thickness{14.0F, 12.0F, 14.0F, 12.0F};
          field.layout.horizontalAlignment = HorizontalAlignment::Start;
          field.layout.verticalAlignment = VerticalAlignment::Start;
          field.paintsBackground = true;
          field.background = Color{0.09F, 0.13F, 0.2F, 1.0F};
          field.text.fontSize = 18.0F;
          field.text.color = Color{0.97F, 0.98F, 1.0F, 1.0F};
          field.text.layout = text.get();
          field.text.geometry = text.get();
          field.text.glyphAtlas = text.get();
          field.text.wrapping = TextWrapping::NoWrap;
          field.textField.selectionColor = Color{0.16F, 0.5F, 0.95F, 0.5F};
          field.textField.caretColor = Color{0.85F, 0.93F, 1.0F, 1.0F};
          field.semantics.label = String{"Name"};
          field.textField.onError = [](const UIError &error) {
            static_cast<void>(ReportError("Text field error", error));
          };
          composer.TextField(Bind(name), *text, field, "name");

          String greeting{"Hello, "};
          greeting.Append(name.Get());
          greeting.Append(" \xE2\x80\x94 shaped as Unicode text.");
          ComposeText(composer, *text, std::move(greeting), 22.0F,
                      Color{0.95F, 0.8F, 0.45F, 1.0F}, "greeting");

          ComposeButton(
              composer, *text, "Activate retained button",
              [&] {
                ++activationCount;
                mainWindow->Invalidate(InvalidationKind::All);
              },
              "activate");
          ComposeText(composer, *text, CounterText(activationCount), 15.0F,
                      Color{0.7F, 0.77F, 0.86F, 1.0F}, "counter");
          ComposeText(
              composer, *text,
              String{"Resize the window to exercise layout and the native "
                     "surface lifecycle."},
              14.0F, Color{0.55F, 0.63F, 0.74F, 1.0F}, "hint");
        },
        "gallery-root");
  });

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
