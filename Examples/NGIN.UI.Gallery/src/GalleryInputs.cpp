#include "GallerySamplePicker.hpp"

#include <array>
#include <cmath>

namespace NGIN::UIGallery::Pages {
using namespace NGIN::UI;
using NGIN::Text::String;

namespace {
enum class InputSample { Preferences, Level, Text, Form };

auto TrackList(GalleryViewModel &model, const Theme &theme) -> Views::View {
  constexpr std::array titles{"Morning train", "Open water", "Return trip"};
  constexpr std::array durations{"3:42", "4:16", "2:58"};
  const bool numbered = model.CheckBinding().Get() == CheckState::Checked;
  const bool showTime = model.ToggleBinding().Get();
  NodeProperties list{};
  list.layout.padding = Thickness::Uniform(Dp{14});
  list.layout.preferredSize.width = 265;
  switch (model.DensityBinding().Get()) {
  case Density::Compact:
    list.layout.gap = 5;
    break;
  case Density::Comfortable:
    list.layout.gap = 10;
    break;
  case Density::Spacious:
    list.layout.gap = 15;
    break;
  }
  list.visual.base.background = theme.colors.sunkenSurface;
  std::vector<Views::View> tracks;
  for (std::size_t i = 0; i < titles.size(); ++i) {
    String title = numbered ? Number(i + 1) : String{};
    if (numbered)
      title.Append(String{"   "});
    title.Append(String{titles[i]});
    NodeProperties line{};
    line.grid.columns = {GridTrack::Weighted(), GridTrack::Auto()};
    auto duration = TextProperties(13, theme.colors.mutedForeground);
    duration.gridPlacement.column = 1;
    std::vector<Views::View> contents{
        TextView(std::move(title), 15, theme.colors.foreground, "track-title")};
    if (showTime)
      contents.push_back(Views::Label(durations[i], duration));
    tracks.push_back(Views::Grid(std::move(contents), line).Key(titles[i]));
  }
  return Views::Column(std::move(tracks), list).Key("track-preview");
}

auto Preferences(GalleryViewModel &model, const Theme &theme) -> Views::View {
  const ControlPresentation style{.theme = theme};
  NodeProperties controls{};
  controls.layout.gap = 8;
  std::vector<Views::View> settings{
      ControlRowView(
          theme, "Track numbers", "settings-check",
          [&](auto p) {
            return Views::CheckBox(model.CheckBinding(), style, p);
          },
          "track-numbers"),
      ControlRowView(
          theme, "Duration", "updates-toggle",
          [&](auto p) {
            return Views::ToggleSwitch(model.ToggleBinding(), style, p);
          },
          "duration")};
  const auto density = [&](const char *name, const char *id, Density value) {
    return ControlRowView(
        theme, name, id,
        [&](auto p) {
          return Views::RadioButton(BindRadio(model.DensityBinding(), value),
                                    style, p);
        },
        id);
  };
  settings.push_back(
      density("Compact spacing", "density-compact", Density::Compact));
  settings.push_back(
      density("Standard spacing", "density-comfortable", Density::Comfortable));
  settings.push_back(
      density("Roomy spacing", "density-spacious", Density::Spacious));
  return SampleCard(
      theme,
      {TextView("Playlist preferences", 22, theme.colors.foreground,
                "preferences-title", SemanticRole::Heading),
       ActionRow({Views::Column(std::move(settings), controls),
                  TrackList(model, theme)},
                 "preferences-preview")},
      "preferences", "preferences-card");
}

auto Level(GalleryViewModel &model, const Theme &theme) -> Views::View {
  const auto value = model.SliderValue();
  String percent = Number(static_cast<UInt64>(std::lround(value * 100)));
  percent.Append(String{"%"});
  NodeProperties slider{};
  slider.layout.preferredSize.width = 300;
  slider.semantics.identifier = "volume-slider";
  slider.semantics.label = "Output volume";
  NodeProperties meter{};
  meter.layout.preferredSize = {300, 16};
  meter.semantics.label = "Output level";
  return SampleCard(
      theme,
      {TextView("Output level", 22, theme.colors.foreground, "output-title",
                SemanticRole::Heading),
       TextView(std::move(percent), 46, theme.colors.foreground, "level-value"),
       Views::ProgressBar({.value = value}, {.theme = theme}, meter),
       Views::Slider(model.SliderBinding(),
                     {.minimum = 0, .maximum = 1, .step = 0.05F},
                     {.theme = theme}, slider)
           .Key("output-slider")},
      "output", "output-card");
}

auto TextEntry(GalleryViewModel &model, const Theme &theme) -> Views::View {
  std::vector<Views::View> content{
      TextView("Profile name", 22, theme.colors.foreground, "name-title",
               SemanticRole::Heading),
      TextFieldView(model, theme, model.NameBinding(), "Profile name",
                    "editable"),
      TextView("Read-only mirror", 14, theme.colors.mutedForeground,
               "mirror-label"),
      TextFieldView(model, theme, model.NameBinding(), "Read-only profile name",
                    "readonly", true)};
  if (auto *help = model.HelpToolTip()) {
    content.push_back(ButtonView(
        theme, "Clear name",
        [&model] {
          auto result = model.NameBinding().Set(String{});
          if (!result)
            model.Report(result.error());
        },
        "tooltip-target", 125, ButtonAppearance::Neutral, true, false, help));
    content.push_back(
        Views::ToolTip(
            *help, CardView(theme,
                            {TextView("Clear the name in both fields.", 14,
                                      theme.colors.foreground, "tooltip-text")},
                            "tooltip-card", 240))
            .Key("delayed-tooltip"));
  }
  return SampleCard(theme, std::move(content), "text-entry", "entry-card");
}

auto Validation(GalleryViewModel &model, const Theme &theme) -> Views::View {
  std::vector<Views::View> content{
      TextView("Create a profile", 22, theme.colors.foreground,
               "validation-title", SemanticRole::Heading),
      TextView("Try “admin” to check an unavailable name.", 14,
               theme.colors.mutedForeground, "name-hint")};
  const auto field = [&](const char *name, const char *key,
                         FieldFeedback feedback, Views::View editor) {
    content.push_back(TextView(name, 15, theme.colors.foreground,
                               std::string{key} + "-label"));
    content.push_back(std::move(editor));
    content.push_back(TextView(std::move(feedback.message), 13,
                               feedback.invalid ? theme.colors.error
                                                : theme.colors.mutedForeground,
                               std::string{key} + "-message"));
  };
  const auto singleLine = [&](const char *name, Binding<String> binding,
                              const char *key, FieldFeedback feedback,
                              bool masked = false) {
    auto editor = TextFieldView(model, theme, std::move(binding), name, key,
                                false, true, feedback.invalid, masked);
    field(name, key, std::move(feedback), std::move(editor));
  };
  singleLine("Display name", model.NameBinding(), "validation-name",
             model.NameFeedback());
  singleLine("Password", model.PasswordBinding(), "validation-password",
             model.PasswordFeedback(), true);
  auto notes = model.NotesFeedback();
  auto editor = NotesEditor(model, theme, "Notes", "validation-notes", 138,
                            notes.invalid);
  field("Notes", "validation-notes", std::move(notes), std::move(editor));
  content.push_back(ActionRow(
      {ButtonView(
           theme, "Check form", [&model] { model.ValidateForm(); },
           "validation-check", 124, ButtonAppearance::Neutral,
           !model.IsFormValidating() && !model.IsFormSaving()),
       CommandButtonView(theme, "Save", model.FormSaveBinding(),
                         "validation-save", 82),
       ButtonView(
           theme, "Simulate error", [&model] { model.StartFailingFormSave(); },
           "validation-fail", 140, ButtonAppearance::Neutral,
           model.FormSaveBinding().CanExecute()),
       ButtonView(
           theme, "Cancel", [&model] { model.CancelFormSave(); },
           "validation-cancel", 92, ButtonAppearance::Subtle,
           model.IsFormSaving())},
      "validation-actions"));
  content.push_back(TextView(model.ValidationSummary(), 14,
                             theme.colors.foreground, "validation-summary"));
  content.push_back(TextView(model.FormSaveStatus(), 14,
                             theme.colors.mutedForeground,
                             "validation-save-status"));
  return SampleCard(theme, std::move(content), "validation", "validation-card");
}
} // namespace

auto InputsView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    auto choice = scope.Local("sample", InputSample::Preferences);
    Views::View sample;
    switch (choice.Get()) {
    case InputSample::Preferences:
      sample = Preferences(model, theme);
      break;
    case InputSample::Level:
      sample = Level(model, theme);
      break;
    case InputSample::Text:
      sample = TextEntry(model, theme);
      break;
    case InputSample::Form:
      sample = Validation(model, theme);
      break;
    }
    return PageContentView(
        {PageHeadingView(
             theme, "Inputs",
             "Change a setting and see it reflected in the sample."),
         SamplePicker(
             theme, choice, "Input samples",
             {{InputSample::Preferences, "Preferences", "inputs-preferences"},
              {InputSample::Level, "Volume", "inputs-level"},
              {InputSample::Text, "Text entry", "inputs-text"},
              {InputSample::Form, "Validation", "inputs-form"}}),
         std::move(sample)});
  });
}

auto AsyncDataView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  const auto key = model.AsyncDemoKey();
  auto select = [&](const char *label, const char *source, const char *id) {
    return ButtonView(
        theme, label, [&model, source] { model.SelectAsyncDemo(source); }, id,
        116,
        key.View() == source ? ButtonAppearance::Accent
                             : ButtonAppearance::Neutral,
        true, key.View() == source);
  };
  std::vector<Views::View> mailbox{ActionRow(
      {select("Inbox", "inbox", "async-items"),
       select("Empty", "empty", "async-empty"),
       select("Offline", "broken", "async-error"),
       ButtonView(
           theme, "Switch rapidly", [&model] { model.RunRapidAsyncDemo(); },
           "async-rapid", 152, ButtonAppearance::Subtle)},
      "async-choices")};
  const auto state = model.AsyncDemoState();
  const char *heading = "Ready to load";
  switch (state) {
  case AsyncPresentationKind::Loading:
    heading = "Checking inbox…";
    break;
  case AsyncPresentationKind::Content:
    heading = "Inbox";
    break;
  case AsyncPresentationKind::Empty:
    heading = "No messages";
    break;
  case AsyncPresentationKind::Error:
    heading = "Inbox unavailable";
    break;
  case AsyncPresentationKind::Idle:
    break;
  }
  mailbox.push_back(TextView(heading, 24, theme.colors.foreground,
                             "async-state-title", SemanticRole::Heading));
  if (state == AsyncPresentationKind::Content) {
    for (const auto &message : model.AsyncDemoItems()) {
      mailbox.push_back(
          TextView(message, 16, theme.colors.foreground, message.View()));
    }
  } else {
    mailbox.push_back(TextView(model.AsyncDemoMessage(), 15,
                               theme.colors.mutedForeground,
                               "async-state-message"));
  }
  if (state == AsyncPresentationKind::Loading) {
    NodeProperties bar{};
    bar.layout.preferredSize.width = 300;
    mailbox.push_back(
        Views::ProgressBar({.indeterminate = true}, {.theme = theme}, bar)
            .Key("async-loading-progress"));
    mailbox.push_back(CommandButtonView(
        theme, "Cancel", model.AsyncCancelBinding(), "async-cancel", 108));
  } else {
    mailbox.push_back(CommandButtonView(
        theme, state == AsyncPresentationKind::Content ? "Reload" : "Retry",
        model.AsyncRetryBinding(), "async-retry", 108));
  }
  return PageContentView(
      {PageHeadingView(theme, "Async data",
                       "Switch mailbox responses while a request is running."),
       SampleCard(theme, std::move(mailbox), "mailbox", "async-state-card")});
}
} // namespace NGIN::UIGallery::Pages
