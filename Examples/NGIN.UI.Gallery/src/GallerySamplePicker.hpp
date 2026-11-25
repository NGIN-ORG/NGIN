#pragma once

#include "GalleryViews.hpp"

#include <initializer_list>

namespace NGIN::UIGallery::Pages {

inline auto ActionRow(std::vector<UI::Views::View> actions,
                      std::string_view key) -> UI::Views::View {
  UI::NodeProperties layout{};
  layout.wrapPanel.itemGap = 8;
  layout.wrapPanel.lineGap = 8;
  return UI::Views::WrapPanel(std::move(actions), layout).Key(key);
}

template <typename T> struct SampleChoice {
  T value;
  const char *label;
  const char *key;
};

// Each button remains a keyboard target after a switch. Only the selected
// sample mounts, so hidden editors, animations and lists do no frame work.
template <typename T>
auto SamplePicker(const UI::Theme &theme, UI::Binding<T> selection,
                  const char *label,
                  std::initializer_list<SampleChoice<T>> choices)
    -> UI::Views::View {
  std::vector<UI::Views::View> buttons;
  for (const auto &choice : choices) {
    const bool selected = selection.Get() == choice.value;
    UI::NodeProperties button{};
    button.layout.minimumSize.height = 36;
    button.layout.padding = UI::Thickness{12, 6, 12, 6};
    button.visual =
        UI::MakeButtonVisual(theme, selected ? UI::ButtonAppearance::Accent
                                             : UI::ButtonAppearance::Neutral);
    button.semantics.label = Text::String{choice.label};
    if (selected)
      button.semantics.states |= UI::SemanticStateFlags::Selected;
    auto caption = TextProperties(14, selected ? theme.colors.accentForeground
                                               : theme.colors.foreground);
    caption.layout.verticalAlignment = UI::VerticalAlignment::Center;
    buttons.push_back(UI::Views::Button(
                          [selection, value = choice.value] {
                            static_cast<void>(selection.Set(value));
                          },
                          UI::Views::Label(choice.label, caption), button)
                          .Key(choice.key));
  }
  UI::NodeProperties group{};
  group.semantics.role = UI::SemanticRole::Group;
  group.semantics.label = Text::String{label};
  group.wrapPanel.itemGap = 6;
  group.wrapPanel.lineGap = 6;
  return UI::Views::WrapPanel(std::move(buttons), group).Key("sample-picker");
}

} // namespace NGIN::UIGallery::Pages
