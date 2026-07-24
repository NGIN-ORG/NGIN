#include <NGIN/UIGallery/Gallery.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace NGIN::UIGallery {
namespace {
using NGIN::F32;
using NGIN::Text::String;
using namespace NGIN::UI;

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

Model::Model()
    : name(String{"NGIN"}, [this](const UI::InvalidationKind kind) {
        if (m_window != nullptr) {
          m_window->Invalidate(kind);
        }
      }) {}

void Model::AttachWindow(UI::Window &window) noexcept { m_window = &window; }

void Model::Activate() noexcept {
  ++activationCount;
  if (m_window != nullptr) {
    m_window->Invalidate(UI::InvalidationKind::All);
  }
}

void ComposeMainView(UI::Composer &composer, UI::NativeTextSystem &text,
                     Model &model) {
  using namespace NGIN::UI;

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
        ComposeText(composer, text, String{"NGIN.UI"}, 34.0F,
                    Color{0.55F, 0.78F, 1.0F, 1.0F}, "title");
        ComposeText(composer, text,
                    String{"Retained UI \xC2\xB7 HarfBuzz + FreeType \xC2\xB7 "
                           "SDL3 + SDL_GPU"},
                    17.0F, Color{0.72F, 0.78F, 0.88F, 1.0F}, "subtitle");
        ComposeText(composer, text,
                    String{"Type your name (UTF-8 and IME supported):"}, 15.0F,
                    Color{0.88F, 0.91F, 0.96F, 1.0F}, "name-label");

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
        field.text.layout = &text;
        field.text.geometry = &text;
        field.text.glyphAtlas = &text;
        field.text.wrapping = TextWrapping::NoWrap;
        field.textField.selectionColor = Color{0.16F, 0.5F, 0.95F, 0.5F};
        field.textField.caretColor = Color{0.85F, 0.93F, 1.0F, 1.0F};
        field.semantics.label = String{"Name"};
        composer.TextField(Bind(model.name), text, field, "name");

        String greeting{"Hello, "};
        greeting.Append(model.name.Get());
        greeting.Append(" \xE2\x80\x94 shaped as Unicode text.");
        ComposeText(composer, text, std::move(greeting), 22.0F,
                    Color{0.95F, 0.8F, 0.45F, 1.0F}, "greeting");

        ComposeButton(
            composer, text, "Activate retained button",
            [&model] { model.Activate(); }, "activate");
        ComposeText(composer, text, CounterText(model.activationCount), 15.0F,
                    Color{0.7F, 0.77F, 0.86F, 1.0F}, "counter");
        ComposeText(
            composer, text,
            String{"Resize the window to exercise layout and the native "
                   "surface lifecycle."},
            14.0F, Color{0.55F, 0.63F, 0.74F, 1.0F}, "hint");
      },
      "gallery-root");
}

auto CreateMainWindow(UI::Application &application, UI::NativeTextSystem &text,
                      Model &model) -> UI::UIResult<UI::Window *> {
  auto window = application.CreateWindow(UI::WindowCreateInfo{
      .id = Text::String{"Gallery.Main"},
      .title = Text::String{"NGIN.UI Gallery"},
      .initialSize = UI::PixelSize{900, 640},
      .minimumSize = UI::PixelSize{640, 480},
  });
  if (!window) {
    return std::move(window).Error();
  }
  model.AttachWindow(*window.Value());
  window.Value()->SetContent([&text, &model](UI::Composer &composer) {
    ComposeMainView(composer, text, model);
  });
  return window.Value();
}
} // namespace NGIN::UIGallery
