#include "GallerySamplePicker.hpp"

#include <cmath>
#include <cstdio>

namespace NGIN::UIGallery::Pages {
using namespace UI;
using Text::String;

namespace {
auto Swatch(const Theme &theme, const char *name, Color color) -> Views::View {
  NodeProperties sample{};
  sample.layout.preferredSize = {118, 52};
  sample.visual = MakePanelVisual(theme);
  sample.visual.base.background = color;
  char hex[16]{};
  const auto channel = [](F32 c) {
    return static_cast<unsigned>(std::lround(c * 255));
  };
  std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", channel(color.red),
                channel(color.green), channel(color.blue));
  NodeProperties column{};
  column.layout.gap = 4;
  return Views::Column(
             {Views::Border({}, sample),
              TextView(name, 15, theme.colors.foreground, "name"),
              TextView(hex, 13, theme.colors.mutedForeground, "value")},
             column)
      .Key(name);
}

auto AccentChoice(GalleryViewModel &model, const Theme &theme,
                  GalleryAccent choice, const char *name, const char *key)
    -> Views::View {
  auto binding = model.AccentBinding();
  const bool selected = binding.Get() == choice;
  NodeProperties button{};
  button.layout.padding = {12, 6, 12, 6};
  button.layout.minimumSize.height = theme.controls.regularHeight;
  button.visual = MakeButtonVisual(theme, ButtonAppearance::Neutral);
  button.visual.base.background =
      selected ? theme.colors.selection : theme.colors.raisedSurface;
  button.visual.base.borderColor =
      selected ? theme.colors.focus : theme.colors.border;
  button.visual.states.hovered.background = theme.colors.selection;
  button.visual.states.pressed.background = theme.colors.sunkenSurface;
  button.semantics.label = name;
  if (selected)
    button.semantics.states |= SemanticStateFlags::Selected;

  NodeProperties dot{};
  dot.layout.preferredSize = {12, 12};
  dot.layout.verticalAlignment = VerticalAlignment::Center;
  dot.visual.base.background = AccentColor(choice, model.IsLightTheme());
  dot.visual.base.cornerRadius = CornerRadius::Uniform(Dp{6});
  dot.semantics.hidden = true;
  auto title = TextProperties(14, theme.colors.foreground);
  title.layout.verticalAlignment = VerticalAlignment::Center;
  NodeProperties row{};
  row.layout.gap = 7;
  row.layout.verticalAlignment = VerticalAlignment::Center;
  return Views::Button(
             [binding, choice] { static_cast<void>(binding.Set(choice)); },
             Views::Row({Views::Border({}, dot), Views::Label(name, title)},
                        row),
             button)
      .Key(key);
}
auto GradientPreview(const Theme &theme) -> Views::View {
  return Views::Component([theme](Views::ComponentScope &scope) {
    auto direction = scope.Local("direction", GradientDirection::Horizontal);
    NodeProperties ramp{};
    ramp.layout.preferredSize.height = 80;
    ramp.visual.base.background = LinearGradient{
        theme.colors.accent, theme.colors.selection, direction.Get()};
    ramp.visual.base.cornerRadius =
        CornerRadius::Uniform(Dp{theme.radii.regular});
    ramp.semantics.role = SemanticRole::Image;
    ramp.semantics.label = "Gradient from accent to selection color";
    return SampleCard(
        theme,
        {TextView("Gradient direction", 22, theme.colors.foreground,
                  "gradient-title", SemanticRole::Heading),
         Views::Border({}, ramp).Key("accent-ramp"),
         SamplePicker(
             theme, direction, "Gradient direction",
             {{GradientDirection::Horizontal, "Horizontal",
               "gradient-horizontal"},
              {GradientDirection::Vertical, "Vertical", "gradient-vertical"},
              {GradientDirection::DiagonalDown, "Downward diagonal",
               "gradient-diagonal-down"},
              {GradientDirection::DiagonalUp, "Upward diagonal",
               "gradient-diagonal-up"}})},
        "gradient", "gradient-card");
  });
}
} // namespace

auto ResourcesView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  auto resources = std::make_shared<ResourceScope>();
  resources->Provide(ThemeResource, theme);
  const auto &palette = resources->Resolve(ThemeResource)->colors;
  const ControlPresentation controls{.theme = theme};

  auto editor = SampleCard(
      theme,
      {
          TextView("Appearance", 22, palette.foreground, "appearance-heading",
                   SemanticRole::Heading),
          ActionRow({AccentChoice(model, theme, GalleryAccent::Cobalt, "Cobalt",
                                  "accent-cobalt"),
                     AccentChoice(model, theme, GalleryAccent::Jade, "Jade",
                                  "accent-jade"),
                     AccentChoice(model, theme, GalleryAccent::Mulberry,
                                  "Mulberry", "accent-mulberry")},
                    "accent-choices"),
          ControlRowView(
              theme, "Gradient controls", "theme-gradients",
              [&](auto p) {
                return Views::ToggleSwitch(model.GradientBinding(), controls,
                                           p);
              },
              "finish-row"),
          ButtonView(
              theme,
              model.IsLightTheme() ? "Use dark theme" : "Use light theme",
              [&model] { model.ToggleTheme(); }, "toggle-theme", 160),
      },
      "appearance", "appearance-card");

  auto colors =
      SampleCard(theme,
                 {TextView("Interaction colors", 22, palette.foreground,
                           "resources-title", SemanticRole::Heading),
                  ActionRow({Swatch(theme, "Accent", palette.accent),
                             Swatch(theme, "Hover", palette.accentHovered),
                             Swatch(theme, "Pressed", palette.accentPressed),
                             Swatch(theme, "Focus", palette.focus)},
                            "theme-swatches")},
                 "palette", "resources-card");
  return PageContentView(
      {PageHeadingView(theme, "Themes",
                       "Choose an accent and finish for the "
                       "controls throughout the gallery."),
       Views::Column(
           {std::move(editor), GradientPreview(theme), std::move(colors)},
           NodeProperties{.layout = {.gap = 16}, .resources = resources})
           .Key("resource-scope")});
}
} // namespace NGIN::UIGallery::Pages
