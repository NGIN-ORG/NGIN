#pragma once

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Controls.hpp>

namespace NGIN::UI {
void CheckBox(Composer &composer, Binding<CheckState> value,
              const ControlPresentation &presentation = {},
              const NodeProperties &properties = {}, std::string_view key = {});
void RadioButton(Composer &composer, RadioSelection selection,
                 const ControlPresentation &presentation = {},
                 const NodeProperties &properties = {},
                 std::string_view key = {});
void ToggleSwitch(Composer &composer, Binding<bool> value,
                  const ControlPresentation &presentation = {},
                  const NodeProperties &properties = {},
                  std::string_view key = {});
void Slider(Composer &composer, Binding<F32> value, SliderRange range = {},
            const ControlPresentation &presentation = {},
            const NodeProperties &properties = {}, std::string_view key = {});
void ProgressBar(Composer &composer, ProgressValue value,
                 const ControlPresentation &presentation = {},
                 const NodeProperties &properties = {},
                 std::string_view key = {});

void Label(Composer &composer, NGIN::Text::String value, ITextLayout &layout,
           IGlyphAtlas &glyphAtlas, std::string_view identifier,
           std::string_view targetIdentifier,
           const NodeProperties &properties = {}, std::string_view key = {});
} // namespace NGIN::UI
