#include "GalleryViews.hpp"

#include <utility>

namespace NGIN::UIGallery::Pages {
using namespace NGIN::UI;
using NGIN::Text::String;

[[nodiscard]] auto Number(const std::uint64_t value) -> String {
  const auto formatted = std::to_string(value);
  return String{formatted.c_str()};
}

[[nodiscard]] auto LabeledNumber(const char *label, const std::uint64_t value)
    -> String {
  String result{label};
  result.Append(Number(value));
  return result;
}

[[nodiscard]] auto TextProperties(const F32 fontSize, const Color color)
    -> NodeProperties {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = fontSize;
  properties.text.color = color;
  properties.text.wrapping = TextWrapping::Wrap;
  return properties;
}

auto TextView(String value, const F32 fontSize, const Color color,
              const std::string_view key, const SemanticRole role)
    -> Views::View {
  auto properties = TextProperties(fontSize, color);
  properties.semantics.role = role;
  return Views::Label(std::move(value), properties).Key(key);
}

auto ButtonView(const Theme &theme, const char *label,
                NGIN::Utilities::Callable<void()> onActivate,
                const std::string_view key, const F32 width, const bool enabled,
                const bool selected, ToolTipController *toolTip)
    -> Views::View {
  NodeProperties button{};
  button.layout.preferredSize = Size{width, theme.controls.regularHeight};
  button.layout.padding = Thickness{14.0F, 8.0F, 14.0F, 8.0F};
  button.layout.horizontalAlignment = HorizontalAlignment::Start;
  button.layout.verticalAlignment = VerticalAlignment::Start;
  button.interaction.enabled = enabled;
  button.semantics.label = String{label};
  button.visual = MakeButtonVisual(theme);
  if (selected) {
    button.visual.state |= VisualStateFlags::Selected;
    button.visual.states.selected.background = theme.colors.raisedSurface;
    button.visual.states.selected.foreground = theme.colors.foreground;
    button.visual.states.selected.borderColor = theme.colors.focus;
  }
  if (toolTip != nullptr) {
    toolTip->Attach(button);
  }

  const auto labelColor = enabled ? (selected ? theme.colors.foreground
                                              : theme.colors.accentForeground)
                                  : theme.colors.disabledForeground;
  auto caption = TextProperties(theme.typography.body, labelColor);
  caption.layout.horizontalAlignment = HorizontalAlignment::Center;
  caption.layout.verticalAlignment = VerticalAlignment::Center;
  return Views::Button(std::move(onActivate),
                       Views::Label(String{label}, caption).Key("label"),
                       button)
      .Key(key);
}

auto CommandButtonView(const Theme &theme, const char *label,
                       CommandBinding command, const std::string_view key,
                       const F32 width) -> Views::View {
  const auto enabled = command.CanExecute();
  NodeProperties button{};
  button.layout.preferredSize = Size{width, theme.controls.regularHeight};
  button.layout.padding = Thickness{14.0F, 8.0F, 14.0F, 8.0F};
  button.layout.horizontalAlignment = HorizontalAlignment::Start;
  button.layout.verticalAlignment = VerticalAlignment::Start;
  button.semantics.label = String{label};
  button.visual = MakeButtonVisual(theme);

  auto labelProperties = TextProperties(
      theme.typography.body, enabled ? theme.colors.accentForeground
                                     : theme.colors.disabledForeground);
  labelProperties.layout.horizontalAlignment = HorizontalAlignment::Center;
  labelProperties.layout.verticalAlignment = VerticalAlignment::Center;
  return Views::Button(
             std::move(command),
             Views::Label(String{label}, labelProperties).Key("label"), button)
      .Key(key);
}

auto CardView(const Theme &theme, std::vector<Views::View> children,
              const std::string_view key, const F32 width) -> Views::View {
  NodeProperties card{};
  card.layout.preferredSize.width = width;
  card.layout.maximumSize.width = width;
  card.layout.padding = Thickness::Uniform(Dp{18.0F});
  card.layout.horizontalAlignment = HorizontalAlignment::Start;
  card.layout.verticalAlignment = VerticalAlignment::Start;
  card.visual = MakePanelVisual(theme);
  card.visual.base.cornerRadius = CornerRadius::Uniform(Dp{6});
  card.semantics.role = SemanticRole::Group;
  return Views::Border(std::move(children), card).Key(key);
}

auto PageHeadingView(const Theme &theme, const char *title,
                     const char *description) -> Views::View {
  NodeProperties heading{};
  heading.layout.gap = 4;
  heading.layout.preferredSize.width = 760;
  heading.layout.maximumSize.width = 760;
  heading.layout.horizontalAlignment = HorizontalAlignment::Start;
  auto descriptionProperties = TextProperties(16, theme.colors.mutedForeground);
  descriptionProperties.layout.maximumSize.width = 720;
  descriptionProperties.text.lineHeight = 24;
  descriptionProperties.semantics.role = SemanticRole::Text;
  return Views::Column({TextView(String{title}, 36, theme.colors.foreground,
                                 "page-title", SemanticRole::Heading),
                        Views::Label(String{description}, descriptionProperties)
                            .Key("page-description")},
                       heading)
      .Key("page-heading");
}

auto TextFieldView(GalleryViewModel &model, const Theme &theme,
                   Binding<String> value, const char *label,
                   const std::string_view key, const bool readOnly,
                   const bool enabled, const bool invalid, const bool password,
                   const F32 width) -> Views::View {
  NodeProperties field{};
  field.layout.preferredSize = Size{width, theme.controls.spaciousHeight};
  field.layout.maximumSize.width = width;
  field.layout.padding = Thickness{12.0F, 10.0F, 12.0F, 10.0F};
  field.layout.horizontalAlignment = HorizontalAlignment::Start;
  field.layout.verticalAlignment = VerticalAlignment::Start;
  field.interaction.enabled = enabled;
  field.visual = MakeTextFieldVisual(theme);
  if (invalid) {
    field.visual.state |= VisualStateFlags::Invalid;
  }
  field.text.fontSize = 17.0F;
  field.text.color =
      enabled ? theme.colors.foreground : theme.colors.disabledForeground;
  field.text.wrapping = TextWrapping::NoWrap;
  field.textField.selectionColor = theme.colors.selection;
  field.textField.caretColor = theme.colors.focus;
  field.textField.compositionColor = theme.colors.focus;
  field.textField.readOnly = readOnly;
  field.textField.password = password;
  field.textField.onError = [&model](const UIError &error) {
    model.Report(error);
  };
  field.semantics.label = String{label};
  return Views::TextField(String{label}, std::move(value), field).Key(key);
}

void StyleScrollView(NodeProperties &properties, const Theme &theme) {
  properties.interaction.focusable = true;
  properties.scroll.scrollbarTrack = theme.colors.sunkenSurface;
  properties.scroll.scrollbarThumb = theme.colors.border;
  properties.scroll.scrollbarThumbHovered = theme.colors.focus;
}

auto PageContentView(std::vector<Views::View> children) -> Views::View {
  NodeProperties properties{};
  properties.layout.gap = 24.0F;
  properties.layout.horizontalAlignment = HorizontalAlignment::Stretch;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  return Views::Column(std::move(children), properties)
      .Key("gallery-page-content");
}

auto SampleCard(const Theme &theme, std::vector<Views::View> children,
                std::string_view columnKey, std::string_view cardKey)
    -> Views::View {
  NodeProperties column{};
  column.layout.gap = theme.spacing.regular;
  return CardView(theme,
                  {Views::Column(std::move(children), column).Key(columnKey)},
                  cardKey);
}
} // namespace NGIN::UIGallery::Pages
