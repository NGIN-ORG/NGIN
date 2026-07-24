#include <NGIN/UIGallery/CustomControls.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace NGIN::UIGallery {
namespace {
using NGIN::F32;
using NGIN::Text::String;
using namespace NGIN::UI;

constexpr std::array<std::string_view, PageCount> PageNames{
    "Overview", "Layout",  "Typography", "Inputs",      "Collections",
    "Overlays", "Windows", "Resources",  "Diagnostics",
};

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

[[nodiscard]] auto TextProperties(NativeTextSystem &text, const F32 fontSize,
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
                 const std::string_view key,
                 const SemanticRole role = SemanticRole::Text) {
  auto properties = TextProperties(text, fontSize, color);
  properties.semantics.role = role;
  composer.Text(std::move(value), text, text, properties, key);
}

void ComposeButton(Composer &composer, NativeTextSystem &text,
                   const Theme &theme, const char *label,
                   NGIN::Utilities::Callable<void()> onActivate,
                   const std::string_view key, const F32 width = 210.0F,
                   const bool enabled = true, const bool selected = false) {
  NodeProperties button{};
  button.layout.preferredSize = Size{width, theme.controls.regularHeight};
  button.layout.padding = Thickness{14.0F, 8.0F, 14.0F, 8.0F};
  button.layout.horizontalAlignment = HorizontalAlignment::Start;
  button.layout.verticalAlignment = VerticalAlignment::Start;
  button.interaction.enabled = enabled;
  button.interaction.focusable = true;
  button.interaction.onActivate = std::move(onActivate);
  button.semantics.role = SemanticRole::Button;
  button.semantics.label = String{label};
  button.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Focus;
  button.visual = MakeButtonVisual(theme);
  if (selected) {
    button.visual.state |= VisualStateFlags::Selected;
    button.visual.states.selected.background = theme.colors.raisedSurface;
    button.visual.states.selected.foreground = theme.colors.foreground;
    button.visual.states.selected.borderColor = theme.colors.focus;
  }

  const auto labelColor = enabled ? (selected ? theme.colors.foreground
                                              : theme.colors.accentForeground)
                                  : theme.colors.disabledForeground;
  composer.Element(
      ElementType::Button, button,
      [&] {
        auto labelProperties =
            TextProperties(text, theme.typography.body, labelColor);
        labelProperties.layout.horizontalAlignment =
            HorizontalAlignment::Center;
        labelProperties.layout.verticalAlignment = VerticalAlignment::Center;
        composer.Text(String{label}, text, text, labelProperties, "label");
      },
      key);
}

template <typename ComposeContent>
void ComposeCard(Composer &composer, const Theme &theme,
                 ComposeContent &&composeContent, const std::string_view key,
                 const F32 width = 720.0F) {
  NodeProperties card{};
  card.layout.preferredSize.width = width;
  card.layout.maximumSize.width = width;
  card.layout.padding = Thickness::Uniform(Dp{theme.spacing.spacious});
  card.layout.horizontalAlignment = HorizontalAlignment::Start;
  card.layout.verticalAlignment = VerticalAlignment::Start;
  card.visual = MakePanelVisual(theme);
  card.semantics.role = SemanticRole::Group;
  composer.Border(std::forward<ComposeContent>(composeContent), card, key);
}

void ComposePageHeading(Composer &composer, NativeTextSystem &text,
                        const Theme &theme, const char *title,
                        const char *description) {
  ComposeText(composer, text, String{title}, 30.0F, theme.colors.foreground,
              "page-title", SemanticRole::Heading);
  ComposeText(composer, text, String{description}, theme.typography.body,
              theme.colors.mutedForeground, "page-description");
  NodeProperties separator{};
  separator.layout.preferredSize.width = 720.0F;
  separator.layout.horizontalAlignment = HorizontalAlignment::Start;
  separator.visual = MakeSeparatorVisual(theme);
  composer.Separator(SeparatorOrientation::Horizontal, separator,
                     "page-separator");
}

void ComposeTextField(Composer &composer, NativeTextSystem &text, Model &model,
                      const Theme &theme, Binding<String> value,
                      const char *label, const std::string_view key,
                      const bool readOnly = false, const bool enabled = true,
                      const bool invalid = false, const bool password = false) {
  NodeProperties field{};
  field.layout.preferredSize = Size{430.0F, theme.controls.spaciousHeight};
  field.layout.maximumSize.width = 520.0F;
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
  field.text.layout = &text;
  field.text.geometry = &text;
  field.text.glyphAtlas = &text;
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
  composer.TextField(std::move(value), text, field, key);
}

void ComposeSwatch(Composer &composer, const Theme &theme, const Color color,
                   const char *label, const std::string_view key) {
  NodeProperties swatch{};
  swatch.layout.preferredSize = Size{130.0F, 74.0F};
  swatch.layout.padding = Thickness::Uniform(Dp{theme.spacing.regular});
  swatch.layout.horizontalAlignment = HorizontalAlignment::Start;
  swatch.layout.verticalAlignment = VerticalAlignment::Start;
  swatch.visual = MakePanelVisual(theme);
  swatch.visual.base.background = color;
  swatch.semantics.role = SemanticRole::Group;
  swatch.semantics.label = String{label};
  composer.Border([] {}, swatch, key);
}

void ComposeOverviewPage(Composer &composer, NativeTextSystem &text,
                         Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "NGIN.UI control gallery",
      "A public-API catalogue for retained controls, layout, native windows, "
      "resources, and diagnostics.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Architecture"}, 19.0F,
                          theme.colors.foreground, "architecture-title",
                          SemanticRole::Heading);
              ComposeText(composer, text,
                          String{"C++23 retained composition \xC2\xB7 "
                                 "HarfBuzz + FreeType \xC2\xB7 SDL3 + SDL_GPU"},
                          theme.typography.body, theme.colors.mutedForeground,
                          "architecture-value");
              ComposeText(
                  composer, text,
                  String{"The same ComposeMainView() runs standalone and "
                         "through NGIN.Core hosting."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "hosting-value");
            },
            "architecture-column");
      },
      "architecture-card");

  NodeProperties metricsRow{};
  metricsRow.layout.gap = theme.spacing.regular;
  metricsRow.layout.horizontalAlignment = HorizontalAlignment::Start;
  metricsRow.layout.verticalAlignment = VerticalAlignment::Start;
  composer.Row(
      [&] {
        ComposeSwatch(composer, theme, theme.colors.accent, "Accent",
                      "accent-swatch");
        ComposeSwatch(composer, theme, theme.colors.selection, "Selection",
                      "selection-swatch");
        ComposeSwatch(composer, theme, theme.colors.focus, "Focus",
                      "focus-swatch");
      },
      "token-swatches");

  ComposeButton(
      composer, text, theme, "Activate retained state",
      [&model] { model.Activate(); }, "overview-activate", 250.0F);
  ComposeText(composer, text,
              LabeledNumber("Activation count: ", model.ActivationCount()),
              theme.typography.body, theme.colors.mutedForeground,
              "activation-count");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Public custom controls"},
                          19.0F, theme.colors.foreground, "custom-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"Badge, progress ring, and interactive bar chart "
                         "implemented without backend changes."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "custom-description");
              ComposeCustomControlExamples(composer, text, theme);
            },
            "custom-column");
      },
      "custom-controls-card");
}

void ComposeLayoutPage(Composer &composer, NativeTextSystem &text,
                       const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Layout",
      "Rows, columns, overlays, padding, borders, alignment, flex sizing, and "
      "nested scrolling.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Row + flex growth"}, 18.0F,
                          theme.colors.foreground, "row-title",
                          SemanticRole::Heading);
              NodeProperties row{};
              row.layout.gap = theme.spacing.regular;
              row.layout.preferredSize.width = 680.0F;
              composer.Row(
                  [&] {
                    ComposeSwatch(composer, theme, theme.colors.accent, "Fixed",
                                  "fixed");
                    NodeProperties flexible{};
                    flexible.layout.preferredSize = Size{180.0F, 74.0F};
                    flexible.layout.flexGrow = 1.0F;
                    flexible.layout.flexShrink = 1.0F;
                    flexible.visual = MakePanelVisual(theme);
                    flexible.visual.base.background =
                        theme.colors.raisedSurface;
                    composer.Border([] {}, flexible, "flexible");
                    ComposeSwatch(composer, theme, theme.colors.error, "Fixed",
                                  "fixed-end");
                  },
                  "flex-row");
            },
            "layout-column");
      },
      "row-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties scroll{};
        scroll.layout.preferredSize = Size{680.0F, 190.0F};
        scroll.layout.maximumSize = Size{680.0F, 190.0F};
        scroll.layout.horizontalAlignment = HorizontalAlignment::Start;
        scroll.layout.verticalAlignment = VerticalAlignment::Start;
        scroll.scroll.vertical = true;
        composer.ScrollView(
            [&] {
              NodeProperties list{};
              list.layout.gap = theme.spacing.regular;
              list.layout.padding = Thickness::Uniform(Dp{4.0F});
              composer.Column(
                  [&] {
                    for (std::uint32_t index = 1; index <= 12; ++index) {
                      ComposeText(
                          composer, text,
                          LabeledNumber("Scrollable retained row ", index),
                          theme.typography.body, theme.colors.foreground,
                          std::to_string(index));
                    }
                  },
                  "scroll-content");
            },
            scroll, "nested-scroll");
      },
      "scroll-card");
}

void ComposeTypographyPage(Composer &composer, NativeTextSystem &text,
                           const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Typography",
      "HarfBuzz-shaped UTF-8 rendered from the bundled OFL Noto Sans face.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Display 34"}, 34.0F,
                          theme.colors.foreground, "display");
              ComposeText(composer, text, String{"Title 20"}, 20.0F,
                          theme.colors.foreground, "title");
              ComposeText(composer, text,
                          String{"Body 14 — naïve café, Ελληνικά, Кириллица"},
                          14.0F, theme.colors.foreground, "unicode");
              ComposeText(composer, text,
                          String{"Arabic shaping: \xD8\xA7\xD9\x84\xD8\xB3"
                                 "\xD9\x84\xD8\xA7\xD9\x85"},
                          18.0F, theme.colors.foreground, "arabic");
              ComposeText(composer, text,
                          String{"Emoji/graphemes: e\xCC\x81  \xF0\x9F\x91\xA9"
                                 "\xE2\x80\x8D\xF0\x9F\x92\xBB"},
                          18.0F, theme.colors.foreground, "graphemes");
              ComposeText(composer, text,
                          String{"Single-line clipping is the current public "
                                 "scope; multiline arrives in Milestone 14."},
                          13.0F, theme.colors.mutedForeground, "text-scope");
            },
            "typography-column");
      },
      "typography-card");
}

void ComposeInputsPage(Composer &composer, NativeTextSystem &text, Model &model,
                       const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Inputs",
      "Keyboard-accessible buttons and grapheme-aware text fields with "
      "selection, validation presentation, password privacy, clipboard, and "
      "IME.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Button states"}, 18.0F,
                          theme.colors.foreground, "button-states-title",
                          SemanticRole::Heading);
              NodeProperties row{};
              row.layout.gap = theme.spacing.regular;
              composer.Row(
                  [&] {
                    ComposeButton(
                        composer, text, theme, "Normal", [] {}, "normal",
                        150.0F);
                    ComposeButton(
                        composer, text, theme, "Selected", [] {}, "selected",
                        150.0F, true, true);
                    ComposeButton(
                        composer, text, theme, "Disabled", [] {}, "disabled",
                        150.0F, false);
                  },
                  "button-states");
            },
            "buttons-column");
      },
      "buttons-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Editable"}, 14.0F,
                          theme.colors.mutedForeground, "editable-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Name", "editable");
              ComposeText(composer, text, String{"Read-only"}, 14.0F,
                          theme.colors.mutedForeground, "readonly-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Read-only name",
                               "readonly", true);
              ComposeText(composer, text, String{"Validation error"}, 14.0F,
                          theme.colors.mutedForeground, "invalid-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Invalid example",
                               "invalid", false, true, true);
              ComposeText(composer, text, String{"Password"}, 14.0F,
                          theme.colors.mutedForeground, "password-label");
              ComposeTextField(composer, text, model, theme,
                               model.PasswordBinding(), "Password", "password",
                               false, true, false, true);
              ComposeText(composer, text, String{"Disabled"}, 14.0F,
                          theme.colors.mutedForeground, "disabled-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Disabled field",
                               "disabled-field", false, false);
            },
            "fields-column");
      },
      "fields-card");
}

void ComposeCollectionsPage(Composer &composer, NativeTextSystem &text,
                            const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Collections",
      "The current retained scrolling and keyed identity foundation. Semantic "
      "ListView and selection controls arrive in Milestone 13.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties scroll{};
        scroll.layout.preferredSize = Size{680.0F, 320.0F};
        scroll.layout.maximumSize = Size{680.0F, 320.0F};
        scroll.layout.horizontalAlignment = HorizontalAlignment::Start;
        scroll.layout.verticalAlignment = VerticalAlignment::Start;
        composer.ScrollView(
            [&] {
              NodeProperties list{};
              list.layout.gap = theme.spacing.regular;
              composer.Column(
                  [&] {
                    for (std::uint32_t index = 1; index <= 20; ++index) {
                      ComposeCard(
                          composer, theme,
                          [&] {
                            ComposeText(composer, text,
                                        LabeledNumber("Keyed item ", index),
                                        15.0F, theme.colors.foreground,
                                        "item-label");
                          },
                          std::to_string(index), 640.0F);
                    }
                  },
                  "keyed-items");
            },
            scroll, "collection-scroll");
      },
      "collection-card");
}

void ComposeOverlaysPage(Composer &composer, NativeTextSystem &text,
                         Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Overlays",
      "In-window popups use viewport-aware placement, modal focus scopes, and "
      "outside-pointer or Escape dismissal.");
  ComposeButton(
      composer, text, theme,
      model.IsPopupOpen() ? "Popup is open" : "Open modal popup",
      [&model] { model.SetPopupOpen(true); }, "open-popup", 230.0F);

  if (!model.IsPopupOpen()) {
    return;
  }

  NodeProperties popup{};
  popup.popup.anchor = Rect{420.0F, 240.0F, 1.0F, 1.0F};
  popup.popup.placement = PopupPlacement::Center;
  popup.popup.modal = true;
  popup.popup.dismissOnEscape = true;
  popup.popup.dismissOnOutsidePointer = true;
  popup.popup.onDismiss = [&model] { model.SetPopupOpen(false); };
  popup.semantics.label = String{"Gallery popup"};
  composer.Popup(
      [&] {
        ComposeCard(
            composer, theme,
            [&] {
              NodeProperties column{};
              column.layout.gap = theme.spacing.regular;
              composer.Column(
                  [&] {
                    ComposeText(composer, text, String{"Modal popup"}, 22.0F,
                                theme.colors.foreground, "popup-title",
                                SemanticRole::Heading);
                    ComposeText(
                        composer, text,
                        String{"Press Escape, click outside, or use Close."},
                        theme.typography.body, theme.colors.mutedForeground,
                        "popup-help");
                    ComposeButton(
                        composer, text, theme, "Close",
                        [&model] { model.SetPopupOpen(false); }, "close-popup",
                        140.0F);
                  },
                  "popup-column");
            },
            "popup-card", 360.0F);
      },
      popup, "demo-popup");
}

void ComposeWindowsPage(Composer &composer, NativeTextSystem &text,
                        Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Windows",
      "Top-level windows and owner-modal dialog windows are platform surfaces, "
      "not children of the retained control tree.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeButton(
                  composer, text, theme, "Open auxiliary window",
                  [&model] {
                    auto opened = model.OpenAuxiliaryWindow(false);
                    if (!opened) {
                      model.Report(opened.Error());
                    }
                  },
                  "open-window", 240.0F);
              ComposeButton(
                  composer, text, theme, "Open modal dialog",
                  [&model] {
                    auto opened = model.OpenAuxiliaryWindow(true);
                    if (!opened) {
                      model.Report(opened.Error());
                    }
                  },
                  "open-dialog", 240.0F);
              ComposeText(composer, text,
                          String{"A modal dialog blocks input to its owner and "
                                 "restores focus when it closes."},
                          theme.typography.body, theme.colors.mutedForeground,
                          "window-help");
            },
            "window-actions");
      },
      "windows-card");
}

void ComposeResourcesPage(Composer &composer, NativeTextSystem &text,
                          Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Themes and resources",
      "Typed, hierarchical resource scopes keep theme and locale values out of "
      "stringly typed property paths.");
  ComposeButton(
      composer, text, theme,
      model.IsLightTheme() ? "Switch to dark theme" : "Switch to light theme",
      [&model] { model.ToggleTheme(); }, "toggle-theme", 240.0F);

  auto resources = std::make_shared<ResourceScope>();
  resources->Provide(ThemeResource, theme);
  resources->Provide(LocaleResource, std::string{"en-US"});
  resources->Provide(ReducedMotionResource, false);
  composer.Scope(
      resources,
      [&] {
        ComposeCard(
            composer, theme,
            [&] {
              NodeProperties column{};
              column.layout.gap = theme.spacing.regular;
              composer.Column(
                  [&] {
                    ComposeText(composer, text,
                                String{"Scoped resource values"}, 19.0F,
                                theme.colors.foreground, "resources-title",
                                SemanticRole::Heading);
                    ComposeText(composer, text,
                                String{"Theme: typed NGIN.UI.Theme"},
                                theme.typography.body,
                                theme.colors.mutedForeground, "theme-resource");
                    ComposeText(composer, text, String{"Locale: en-US"},
                                theme.typography.body,
                                theme.colors.mutedForeground,
                                "locale-resource");
                    ComposeText(composer, text, String{"Reduced motion: false"},
                                theme.typography.body,
                                theme.colors.mutedForeground,
                                "motion-resource");
                  },
                  "resource-values");
            },
            "resources-card");
      },
      "resource-scope");
}

void ComposeDiagnosticsPage(Composer &composer, NativeTextSystem &text,
                            Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Diagnostics",
      "Live structural counters, semantic output, immutable inspector "
      "snapshots, and optional layout/focus overlays.");
  const auto diagnostics = model.Diagnostics();
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.compact;
        composer.Column(
            [&] {
              ComposeText(composer, text,
                          LabeledNumber("Frames: ", diagnostics.frameCount),
                          theme.typography.body, theme.colors.foreground,
                          "frames");
              ComposeText(
                  composer, text,
                  LabeledNumber("Compositions: ", diagnostics.compositionCount),
                  theme.typography.body, theme.colors.foreground,
                  "compositions");
              ComposeText(composer, text,
                          LabeledNumber("Semantic nodes: ",
                                        diagnostics.semanticNodeCount),
                          theme.typography.body, theme.colors.foreground,
                          "semantic-count");
              ComposeText(composer, text,
                          LabeledNumber("Display commands: ",
                                        diagnostics.displayCommandCount),
                          theme.typography.body, theme.colors.foreground,
                          "display-count");
              ComposeText(
                  composer, text,
                  LabeledNumber("Draw batches: ", diagnostics.drawBatchCount),
                  theme.typography.body, theme.colors.foreground,
                  "batch-count");
            },
            "diagnostic-values");
      },
      "diagnostics-card");
  ComposeButton(
      composer, text, theme,
      model.IsInspectorEnabled() ? "Disable inspector overlay"
                                 : "Enable inspector overlay",
      [&model] { model.ToggleInspector(); }, "toggle-inspector", 260.0F);
}

void ComposePage(Composer &composer, NativeTextSystem &text, Model &model,
                 const Theme &theme) {
  switch (model.CurrentPage()) {
  case Page::Overview:
    ComposeOverviewPage(composer, text, model, theme);
    break;
  case Page::Layout:
    ComposeLayoutPage(composer, text, theme);
    break;
  case Page::Typography:
    ComposeTypographyPage(composer, text, theme);
    break;
  case Page::Inputs:
    ComposeInputsPage(composer, text, model, theme);
    break;
  case Page::Collections:
    ComposeCollectionsPage(composer, text, theme);
    break;
  case Page::Overlays:
    ComposeOverlaysPage(composer, text, model, theme);
    break;
  case Page::Windows:
    ComposeWindowsPage(composer, text, model, theme);
    break;
  case Page::Resources:
    ComposeResourcesPage(composer, text, model, theme);
    break;
  case Page::Diagnostics:
    ComposeDiagnosticsPage(composer, text, model, theme);
    break;
  }
}

void ComposeAuxiliaryWindow(Composer &composer, NativeTextSystem &text,
                            Model &model, const bool modal) {
  const auto theme = model.CurrentTheme();
  NodeProperties root{};
  root.layout.padding = Thickness::Uniform(Dp{theme.spacing.spacious});
  root.layout.gap = theme.spacing.spacious;
  root.visual.base.background = theme.colors.background;
  root.semantics.role = modal ? SemanticRole::Dialog : SemanticRole::Group;
  root.semantics.label =
      String{modal ? "Gallery modal dialog" : "Gallery auxiliary window"};
  composer.Element(
      ElementType::Column, root,
      [&] {
        ComposeText(composer, text,
                    String{modal ? "Modal dialog" : "Auxiliary window"}, 26.0F,
                    theme.colors.foreground, "title", SemanticRole::Heading);
        ComposeText(
            composer, text,
            String{modal ? "Input to the owner is blocked until this closes."
                         : "NGIN.UI supports multiple independent surfaces."},
            theme.typography.body, theme.colors.mutedForeground, "body");
        ComposeText(composer, text,
                    "Close this surface with its native window controls.",
                    theme.typography.caption, theme.colors.mutedForeground,
                    "close-hint");
      },
      "auxiliary-root");
}
} // namespace

auto PageAt(const NGIN::UIntSize index) noexcept -> Page {
  return static_cast<Page>(index % PageCount);
}

auto PageName(const Page page) noexcept -> std::string_view {
  return PageNames[static_cast<NGIN::UIntSize>(page)];
}

Model::Model()
    : m_page(
          Page::Overview,
          [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_lightTheme(
          false, [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_name(String{"NGIN"},
             [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_password(String{"retained"},
                 [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_activationCount(
          0, [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_popupOpen(
          false, [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_inspectorEnabled(
          false, [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_status(String{},
               [this](const InvalidationKind kind) { Invalidate(kind); }) {}

void Model::AttachRuntime(Application &application, NativeTextSystem &text,
                          Window &window) noexcept {
  m_application = &application;
  m_text = &text;
  m_window = &window;
}

auto Model::CurrentPage() const noexcept -> Page { return m_page.Get(); }

void Model::SelectPage(const Page page) {
  SetPopupOpen(false);
  static_cast<void>(m_page.Set(page));
}

auto Model::CurrentTheme() const -> Theme {
  return IsLightTheme() ? MakeLightTheme() : Theme{};
}

auto Model::IsLightTheme() const noexcept -> bool { return m_lightTheme.Get(); }

void Model::ToggleTheme() {
  static_cast<void>(m_lightTheme.Set(!m_lightTheme.Get()));
}

auto Model::Name() const noexcept -> const String & { return m_name.Get(); }

auto Model::NameBinding() -> Binding<String> { return Bind(m_name); }

auto Model::PasswordBinding() -> Binding<String> { return Bind(m_password); }

auto Model::ActivationCount() const noexcept -> std::uint32_t {
  return m_activationCount.Get();
}

void Model::Activate() {
  static_cast<void>(m_activationCount.Set(m_activationCount.Get() + 1));
}

auto Model::IsPopupOpen() const noexcept -> bool { return m_popupOpen.Get(); }

void Model::SetPopupOpen(const bool open) {
  static_cast<void>(m_popupOpen.Set(open));
}

auto Model::IsInspectorEnabled() const noexcept -> bool {
  return m_inspectorEnabled.Get();
}

void Model::ToggleInspector() {
  const auto enabled = !m_inspectorEnabled.Get();
  static_cast<void>(m_inspectorEnabled.Set(enabled));
  if (m_window != nullptr) {
    m_window->SetInspectorOverlay(InspectorOverlayOptions{
        .enabled = enabled,
        .showLayoutBounds = true,
        .showHitTestBounds = false,
        .showFocus = true,
    });
  }
}

auto Model::OpenAuxiliaryWindow(const bool modal) noexcept -> UIResult<void> {
  if (m_application == nullptr || m_text == nullptr || m_window == nullptr) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Gallery runtime is not attached", "NGIN.UI.Gallery",
                       "OpenAuxiliaryWindow");
  }

  ++m_auxiliaryWindowId;
  String id{modal ? "Gallery.Dialog." : "Gallery.Window."};
  id.Append(Number(m_auxiliaryWindowId));
  String title{modal ? "NGIN.UI Modal Dialog" : "NGIN.UI Auxiliary Window"};
  Window *created = nullptr;
  if (modal) {
    auto result = m_application->CreateDialogWindow(
        *m_window, WindowCreateInfo{
                       .id = id,
                       .title = title,
                       .initialSize = PixelSize{520, 300},
                       .minimumSize = PixelSize{420, 240},
                   });
    if (!result) {
      return std::move(result).Error();
    }
    created = result.Value();
  } else {
    auto result = m_application->CreateWindow(WindowCreateInfo{
        .id = id,
        .title = title,
        .initialSize = PixelSize{560, 340},
        .minimumSize = PixelSize{420, 240},
    });
    if (!result) {
      return std::move(result).Error();
    }
    created = result.Value();
  }

  created->SetContent([this, modal](Composer &composer) {
    ComposeAuxiliaryWindow(composer, *m_text, *this, modal);
  });
  static_cast<void>(m_status.Set(
      String{modal ? "Opened modal dialog" : "Opened auxiliary window"}));
  return {};
}

auto Model::Status() const noexcept -> const String & { return m_status.Get(); }

auto Model::Diagnostics() const noexcept -> WindowDiagnostics {
  return m_window != nullptr ? m_window->Diagnostics() : WindowDiagnostics{};
}

void Model::Report(UIError error) {
  static_cast<void>(m_status.Set(std::move(error.message)));
}

void Model::Invalidate(const InvalidationKind kind) const noexcept {
  if (m_window != nullptr) {
    m_window->Invalidate(kind);
  }
}

void ComposeMainView(Composer &composer, NativeTextSystem &text, Model &model) {
  const auto theme = model.CurrentTheme();
  NodeProperties root{};
  root.layout.padding = Thickness::Uniform(Dp{24.0F});
  root.layout.gap = 22.0F;
  root.visual.base.background = theme.colors.background;
  root.semantics.role = SemanticRole::Group;
  root.semantics.label = String{"NGIN.UI control gallery"};

  composer.Element(
      ElementType::Row, root,
      [&] {
        NodeProperties sidebar{};
        sidebar.layout.preferredSize.width = 210.0F;
        sidebar.layout.minimumSize.width = 210.0F;
        sidebar.layout.maximumSize.width = 210.0F;
        sidebar.layout.flexShrink = 0.0F;
        sidebar.layout.padding = Thickness::Uniform(Dp{14.0F});
        sidebar.layout.gap = theme.spacing.regular;
        sidebar.layout.verticalAlignment = VerticalAlignment::Stretch;
        sidebar.visual = MakePanelVisual(theme);
        sidebar.semantics.role = SemanticRole::Group;
        sidebar.semantics.label = String{"Gallery navigation"};
        composer.Element(
            ElementType::Column, sidebar,
            [&] {
              ComposeText(composer, text, String{"NGIN.UI"}, 25.0F,
                          theme.colors.focus, "brand", SemanticRole::Heading);
              ComposeText(composer, text, String{"Control gallery"}, 13.0F,
                          theme.colors.mutedForeground, "brand-subtitle");
              NodeProperties separator{};
              separator.visual = MakeSeparatorVisual(theme);
              composer.Separator(SeparatorOrientation::Horizontal, separator,
                                 "nav-separator");
              for (NGIN::UIntSize index = 0; index < PageCount; ++index) {
                const auto page = PageAt(index);
                const auto name = PageName(page);
                ComposeButton(
                    composer, text, theme, name.data(),
                    [&model, page] { model.SelectPage(page); },
                    std::to_string(index), 180.0F, true,
                    model.CurrentPage() == page);
              }
              ComposeButton(
                  composer, text, theme,
                  model.IsLightTheme() ? "Use dark theme" : "Use light theme",
                  [&model] { model.ToggleTheme(); }, "theme", 180.0F);
            },
            "sidebar");

        NodeProperties viewport{};
        viewport.layout.preferredSize = Size{780.0F, 680.0F};
        viewport.layout.minimumSize = Size{420.0F, 420.0F};
        viewport.layout.flexGrow = 1.0F;
        viewport.layout.flexShrink = 1.0F;
        viewport.layout.horizontalAlignment = HorizontalAlignment::Stretch;
        viewport.layout.verticalAlignment = VerticalAlignment::Stretch;
        viewport.scroll.vertical = true;
        viewport.scroll.horizontal = false;
        composer.ScrollView(
            [&] {
              NodeProperties page{};
              page.layout.padding = Thickness{4.0F, 4.0F, 18.0F, 28.0F};
              page.layout.gap = theme.spacing.spacious;
              page.layout.horizontalAlignment = HorizontalAlignment::Stretch;
              page.layout.verticalAlignment = VerticalAlignment::Start;
              page.semantics.role = SemanticRole::Group;
              page.semantics.label =
                  String{PageName(model.CurrentPage()).data()};
              composer.Element(
                  ElementType::Column, page,
                  [&] {
                    ComposePage(composer, text, model, theme);
                    if (!model.Status().Empty()) {
                      ComposeText(composer, text, model.Status(), 13.0F,
                                  theme.colors.mutedForeground,
                                  "gallery-status");
                    }
                  },
                  "page");
            },
            viewport, "catalogue-viewport");
      },
      "gallery-root");
}

auto CreateMainWindow(Application &application, NativeTextSystem &text,
                      Model &model) -> UIResult<Window *> {
  auto window = application.CreateWindow(WindowCreateInfo{
      .id = String{"Gallery.Main"},
      .title = String{"NGIN.UI Control Gallery"},
      .initialSize = PixelSize{1180, 780},
      .minimumSize = PixelSize{900, 600},
  });
  if (!window) {
    return std::move(window).Error();
  }
  model.AttachRuntime(application, text, *window.Value());
  window.Value()->SetContent([&text, &model](Composer &composer) {
    ComposeMainView(composer, text, model);
  });
  return window.Value();
}
} // namespace NGIN::UIGallery
