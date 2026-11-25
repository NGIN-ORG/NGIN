#pragma once

#include <NGIN/UI/Components.hpp>

namespace NGIN::UI::Views {
[[nodiscard]] auto CheckBox(Binding<CheckState> value,
                            ControlPresentation presentation = {},
                            NodeProperties properties = {}) -> View;
[[nodiscard]] auto RadioButton(RadioSelection selection,
                               ControlPresentation presentation = {},
                               NodeProperties properties = {}) -> View;
[[nodiscard]] auto ToggleSwitch(Binding<bool> value,
                                ControlPresentation presentation = {},
                                NodeProperties properties = {}) -> View;
[[nodiscard]] auto Slider(Binding<F32> value, SliderRange range,
                          ControlPresentation presentation = {},
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto ProgressBar(ProgressValue value,
                               ControlPresentation presentation = {},
                               NodeProperties properties = {}) -> View;
/// @brief Shows the given content when the attached controller opens.
/// The controller must outlive the mounted view.
[[nodiscard]] auto ToolTip(ToolTipController &controller, View content) -> View;
} // namespace NGIN::UI::Views
