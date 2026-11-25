#include <NGIN/UI/ViewControls.hpp>

#include "ComponentPrimitives.hpp"
#include "ControlPrimitives.hpp"

namespace NGIN::UI::Views {
auto CheckBox(Binding<CheckState> value, ControlPresentation presentation,
              NodeProperties properties) -> View {
  return Element(
      [value = std::move(value), presentation = std::move(presentation),
       properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key) {
        if (value.IsReadable())
          static_cast<void>(value.Get());
        UI::CheckBox(composer, value, presentation, properties, key);
      });
}

auto RadioButton(RadioSelection selection, ControlPresentation presentation,
                 NodeProperties properties) -> View {
  return Element(
      [selection = std::move(selection), presentation = std::move(presentation),
       properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key) {
        if (selection.isSelected)
          static_cast<void>(selection.isSelected());
        UI::RadioButton(composer, selection, presentation, properties, key);
      });
}

auto ToggleSwitch(Binding<bool> value, ControlPresentation presentation,
                  NodeProperties properties) -> View {
  return Element(
      [value = std::move(value), presentation = std::move(presentation),
       properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key) {
        if (value.IsReadable())
          static_cast<void>(value.Get());
        UI::ToggleSwitch(composer, value, presentation, properties, key);
      });
}

auto Slider(Binding<F32> value, SliderRange range,
            ControlPresentation presentation, NodeProperties properties)
    -> View {
  return Element(
      [value = std::move(value), range, presentation = std::move(presentation),
       properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key) {
        if (value.IsReadable())
          static_cast<void>(value.Get());
        UI::Slider(composer, value, range, presentation, properties, key);
      });
}

auto ProgressBar(ProgressValue value, ControlPresentation presentation,
                 NodeProperties properties) -> View {
  return Element([value, presentation = std::move(presentation),
                  properties = std::move(properties)](Composer &composer,
                                                      const ViewEnvironment &,
                                                      std::string_view key) {
    UI::ProgressBar(composer, value, presentation, properties, key);
  });
}

auto ToolTip(ToolTipController &controller, View content) -> View {
  return Component([&controller, content = std::move(content)] {
    if (!controller.IsOpen()) {
      return View{};
    }
    NodeProperties popup{};
    popup.popup.anchor = controller.Anchor();
    popup.popup.placement = PopupPlacement::BelowStart;
    popup.popup.gap = 8.0F;
    popup.popup.modal = false;
    popup.popup.dismissOnOutsidePointer = false;
    popup.popup.dismissOnEscape = false;
    popup.interaction.hitTestVisible = false;
    popup.semantics.hidden = true;
    return Box(ElementType::Popup, {content}, std::move(popup));
  });
}
} // namespace NGIN::UI::Views
