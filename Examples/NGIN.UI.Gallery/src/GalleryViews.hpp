#pragma once

#include <NGIN/UIGallery/Gallery.hpp>

#include <string>

namespace NGIN::UIGallery::Pages {
[[nodiscard]] auto Number(const std::uint64_t value) -> Text::String;
[[nodiscard]] auto LabeledNumber(const char *label, const std::uint64_t value)
    -> Text::String;
[[nodiscard]] auto TextProperties(const F32 fontSize, const UI::Color color)
    -> UI::NodeProperties;
auto TextView(Text::String value, const F32 fontSize, const UI::Color color,
              const std::string_view key,
              const UI::SemanticRole role = UI::SemanticRole::Text)
    -> UI::Views::View;
auto ButtonView(const UI::Theme &theme, const char *label,
                NGIN::Utilities::Callable<void()> onActivate,
                const std::string_view key, const F32 width = 210.0F,
                UI::ButtonAppearance appearance = UI::ButtonAppearance::Accent,
                const bool enabled = true, const bool selected = false,
                UI::ToolTipController *toolTip = nullptr) -> UI::Views::View;
auto CommandButtonView(const UI::Theme &theme, const char *label,
                       UI::CommandBinding command, const std::string_view key,
                       const F32 width = 210.0F) -> UI::Views::View;
auto CardView(const UI::Theme &theme, std::vector<UI::Views::View> children,
              const std::string_view key, const F32 width = 760.0F)
    -> UI::Views::View;
auto PageHeadingView(const UI::Theme &theme, const char *title,
                     const char *description) -> UI::Views::View;
auto TextFieldView(GalleryViewModel &model, const UI::Theme &theme,
                   UI::Binding<Text::String> value, const char *label,
                   const std::string_view key, const bool readOnly = false,
                   const bool enabled = true, const bool invalid = false,
                   const bool password = false, const F32 width = 430.0F)
    -> UI::Views::View;
auto NotesEditor(GalleryViewModel &model, const UI::Theme &theme,
                 const char *label, std::string_view key, F32 height,
                 bool invalid = false) -> UI::Views::View;
void StyleScrollView(UI::NodeProperties &properties, const UI::Theme &theme);
auto PageContentView(std::vector<UI::Views::View> children) -> UI::Views::View;
auto SampleCard(const UI::Theme &theme, std::vector<UI::Views::View> children,
                std::string_view columnKey, std::string_view cardKey)
    -> UI::Views::View;

auto OverviewView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto LayoutView(GalleryViewModel &model, const UI::Theme &theme) -> UI::Views::View;
auto TypographyView(const UI::Theme &theme) -> UI::Views::View;
auto TextAreaView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto ImagesView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto TemplatesView(GalleryViewModel &model, const UI::Theme &theme) -> UI::Views::View;
auto InputsView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto AsyncDataView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto CollectionsView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto MotionView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto OverlaysView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto WindowsView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto ResourcesView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto AccessibilityView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;
auto DiagnosticsView(GalleryViewModel &model, const UI::Theme &theme)
    -> UI::Views::View;

template <typename Control>
auto ControlRowView(const UI::Theme &theme, const char *label,
                    std::string_view identifier, Control makeControl,
                    std::string_view key) -> UI::Views::View {
  UI::NodeProperties row{};
  row.wrapPanel.itemGap = theme.spacing.spacious;
  row.wrapPanel.lineGap = theme.spacing.compact;
  row.layout.horizontalAlignment = UI::HorizontalAlignment::Start;
  row.layout.verticalAlignment = UI::VerticalAlignment::Center;
  auto text = TextProperties(theme.typography.body, theme.colors.foreground);
  text.layout.preferredSize.width = text.layout.maximumSize.width = 164;
  text.layout.verticalAlignment = UI::VerticalAlignment::Center;
  const auto labelId = std::string{identifier} + "-label";
  text.semantics.identifier = Text::String{labelId};
  text.semantics.labelFor = Text::String{identifier};
  text.semantics.label = Text::String{label};
  UI::NodeProperties control{};
  control.layout.horizontalAlignment = UI::HorizontalAlignment::Start;
  control.layout.verticalAlignment = UI::VerticalAlignment::Center;
  control.semantics.identifier = Text::String{identifier};
  control.semantics.labelledBy = Text::String{labelId};
  control.semantics.label = Text::String{label};
  return UI::Views::WrapPanel(
             {UI::Views::Label(Text::String{label}, text).Key("label"),
              makeControl(control).Key("control")},
             row)
      .Key(key);
}
} // namespace NGIN::UIGallery::Pages
