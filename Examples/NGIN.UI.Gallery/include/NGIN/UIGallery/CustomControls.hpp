#pragma once

#include <NGIN/UI/UI.hpp>

#include <vector>

namespace NGIN::UIGallery {
class BadgeElement final : public UI::ICustomElement {
public:
  BadgeElement(Text::String label, UI::Color background, UI::Color foreground);

  auto Measure(UI::CustomElementContext &context,
               UI::SizeConstraints constraints)
      -> UI::UIResult<UI::Size> override;
  auto Paint(UI::CustomElementContext &context, UI::PaintContext &paint)
      -> UI::UIResult<void> override;
  auto Semantics(UI::CustomElementContext &context)
      -> UI::UIResult<UI::SemanticProperties> override;

private:
  Text::String m_label;
  UI::Color m_background;
  UI::Color m_foreground;
};

class ProgressRingElement final : public UI::ICustomElement {
public:
  ProgressRingElement(NGIN::F32 progress, UI::Color track, UI::Color value);

  auto Measure(UI::CustomElementContext &context,
               UI::SizeConstraints constraints)
      -> UI::UIResult<UI::Size> override;
  auto Paint(UI::CustomElementContext &context, UI::PaintContext &paint)
      -> UI::UIResult<void> override;
  auto Semantics(UI::CustomElementContext &context)
      -> UI::UIResult<UI::SemanticProperties> override;

private:
  NGIN::F32 m_progress{0.0F};
  UI::Color m_track;
  UI::Color m_value;
};

class BarChartElement final : public UI::ICustomElement {
public:
  BarChartElement(std::vector<NGIN::F32> values, UI::Color track,
                  UI::Color value, UI::Color selected);

  auto Measure(UI::CustomElementContext &context,
               UI::SizeConstraints constraints)
      -> UI::UIResult<UI::Size> override;
  auto Paint(UI::CustomElementContext &context, UI::PaintContext &paint)
      -> UI::UIResult<void> override;
  auto Semantics(UI::CustomElementContext &context)
      -> UI::UIResult<UI::SemanticProperties> override;
  auto PointerEvent(UI::CustomElementContext &context,
                    UI::RoutedPointerEvent &event)
      -> UI::UIResult<UI::InvalidationKind> override;

private:
  std::vector<NGIN::F32> m_values;
  UI::Color m_track;
  UI::Color m_value;
  UI::Color m_selected;
};

/// @brief Custom motion property consumed only by MotionDialElement.
inline const UI::AnimationProperty<NGIN::F32> MotionDialSweep{
    "NGIN.UI.Gallery.MotionDial.Sweep", 0.0F};

/// @brief Custom-painted control that reads a retained animation property.
class MotionDialElement final : public UI::ICustomElement {
public:
  MotionDialElement(UI::Color track, UI::Color value);

  auto Measure(UI::CustomElementContext &context,
               UI::SizeConstraints constraints)
      -> UI::UIResult<UI::Size> override;
  auto Paint(UI::CustomElementContext &context, UI::PaintContext &paint)
      -> UI::UIResult<void> override;
  auto Semantics(UI::CustomElementContext &context)
      -> UI::UIResult<UI::SemanticProperties> override;

private:
  UI::Color m_track;
  UI::Color m_value;
};

void ComposeCustomControlExamples(UI::Composer &composer,
                                  UI::NativeTextSystem &text,
                                  const UI::Theme &theme);
} // namespace NGIN::UIGallery
