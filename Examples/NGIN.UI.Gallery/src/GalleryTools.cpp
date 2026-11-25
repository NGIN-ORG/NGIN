#include "GallerySamplePicker.hpp"

namespace NGIN::UIGallery::Pages {
using namespace UI;
using Text::String;

namespace {
auto WindowAction(GalleryViewModel &model, const Theme &theme, bool modal)
    -> Views::View {
  return ButtonView(
      theme, modal ? "Open dialog" : "Open window",
      [&model, modal] {
        auto result = model.OpenAuxiliaryWindow(modal);
        if (!result)
          model.Report(result.error());
      },
      modal ? "open-dialog" : "open-window", 142);
}
} // namespace

auto OverlaysView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  NodeProperties trigger{};
  trigger.layout.preferredSize = {164, theme.controls.regularHeight};
  trigger.layout.horizontalAlignment = HorizontalAlignment::Start;
  trigger.visual = MakeButtonVisual(theme);
  trigger.semantics.identifier = String{"overlay-trigger"};
  trigger.semantics.label = String{"View details"};
  auto caption = TextProperties(15, theme.colors.accentForeground);
  caption.layout.horizontalAlignment = HorizontalAlignment::Center;
  caption.layout.verticalAlignment = VerticalAlignment::Center;
  auto button = Views::Button([&model] { model.SetPopupOpen(true); },
                              Views::Label("View details", caption), trigger)
                    .Key("open-popup");

  std::vector<Views::View> content{
      PageHeadingView(theme, "Overlays",
                      "Open a file preview beside its trigger."),
      SampleCard(theme,
                 {TextView("Morning train", 24, theme.colors.foreground,
                           "recording-title", SemanticRole::Heading),
                  TextView("Field recording · 3 min 42 sec", 15,
                           theme.colors.mutedForeground, "recording-duration"),
                  std::move(button)},
                 "overlay-file", "overlay-card")};
  if (model.IsPopupOpen()) {
    // Resolve the anchor from the arranged button so the popup follows resizing
    // and scrolling.
    NodeProperties popup{};
    popup.popup.anchorIdentifier = String{"overlay-trigger"};
    popup.popup.placement = PopupPlacement::BelowStart;
    popup.popup.gap = 6;
    popup.popup.modal = true;
    popup.popup.dismissOnEscape = true;
    popup.popup.dismissOnOutsidePointer = true;
    popup.popup.onDismiss = [&model] { model.SetPopupOpen(false); };
    popup.semantics.label = String{"Recording details"};
    NodeProperties details{};
    details.layout.gap = 8;
    content.push_back(
        Views::Box(
            ElementType::Popup,
            {CardView(
                theme,
                {Views::Column(
                    {TextView("Recording details", 20, theme.colors.foreground,
                              "popup-title", SemanticRole::Heading),
                     TextView("Stereo WAV · 48 kHz · 24 bit", 14,
                              theme.colors.mutedForeground, "popup-format"),
                     ButtonView(
                         theme, "Close",
                         [&model] { model.SetPopupOpen(false); }, "close-popup",
                         88)},
                    details)},
                "popup-card", 264)},
            popup)
            .Key("demo-popup"));
  }
  return PageContentView(std::move(content));
}

auto WindowsView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  return PageContentView(
      {PageHeadingView(theme, "Windows",
                       "Compare an independent window with a modal dialog."),
       SampleCard(
           theme,
           {TextView("Independent window", 22, theme.colors.foreground,
                     "window-title", SemanticRole::Heading),
            TextView("Keep using the gallery while this window is open.", 15,
                     theme.colors.mutedForeground, "window-description"),
            WindowAction(model, theme, false)},
           "window-actions", "window-card"),
       SampleCard(
           theme,
           {TextView("Modal dialog", 22, theme.colors.foreground,
                     "dialog-title", SemanticRole::Heading),
            TextView("Close the dialog to return control to the gallery.", 15,
                     theme.colors.mutedForeground, "dialog-description"),
            WindowAction(model, theme, true)},
           "dialog-actions", "dialog-card")});
}

auto AccessibilityView(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  auto live = TextProperties(16, theme.colors.foreground);
  live.semantics.role = SemanticRole::Text;
  live.semantics.label = model.AccessibilityAnnouncement();
  live.semantics.live = SemanticLiveSetting::Polite;
  const auto diagnostics = model.AccessibilityDiagnostics();
  String connection{diagnostics.available ? "Connected provider · "
                                          : "Native provider unavailable · "};
  connection.Append(diagnostics.providerName);
  const ControlPresentation controls{.theme = theme};
  return PageContentView(
      {PageHeadingView(theme, "Accessibility",
                       "Try the controls with a keyboard or screen reader."),
       SampleCard(theme,
                  {TextView("Live announcement", 22, theme.colors.foreground,
                            "announcement-title", SemanticRole::Heading),
                   ButtonView(
                       theme, "Announce message",
                       [&model] { model.AnnounceAccessibilityDemo(); },
                       "announce-button", 172),
                   Views::Label(model.AccessibilityAnnouncement(), live)
                       .Key("accessibility-live-region"),
                   TextView(std::move(connection), 13,
                            theme.colors.mutedForeground, "provider-status")},
                  "announcement", "announcement-card"),
       SampleCard(theme,
                  {TextView("Named controls", 20, theme.colors.foreground,
                            "controls-title", SemanticRole::Heading),
                   ControlRowView(
                       theme, "Notifications", "accessibility-check",
                       [&](auto control) {
                         return Views::CheckBox(model.CheckBinding(), controls,
                                                control);
                       },
                       "accessibility-check-row"),
                   ControlRowView(
                       theme, "Automatic updates", "accessibility-switch",
                       [&](auto control) {
                         return Views::ToggleSwitch(model.ToggleBinding(),
                                                    controls, control);
                       },
                       "accessibility-switch-row"),
                   ControlRowView(
                       theme, "Volume", "accessibility-slider",
                       [&](auto control) {
                         control.layout.preferredSize.width = 180;
                         return Views::Slider(
                             model.SliderBinding(),
                             {.minimum = 0, .maximum = 1, .step = .05F},
                             controls, control);
                       },
                       "accessibility-slider-row")},
                  "accessibility-controls", "accessibility-controls-card")});
}
} // namespace NGIN::UIGallery::Pages
