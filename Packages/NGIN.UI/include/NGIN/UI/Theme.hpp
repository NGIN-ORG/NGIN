#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Style.hpp>

namespace NGIN::UI {
/// @brief Semantic colors used to derive default control styles.
struct ColorPalette final {
  Color background{0.08F, 0.09F, 0.11F, 1.0F};
  Color surface{0.14F, 0.15F, 0.18F, 1.0F};
  Color raisedSurface{0.18F, 0.19F, 0.23F, 1.0F};
  Color sunkenSurface{0.055F, 0.06F, 0.08F, 1.0F};
  Color foreground{0.94F, 0.95F, 0.97F, 1.0F};
  Color mutedForeground{0.65F, 0.68F, 0.72F, 1.0F};
  Color accent{0.20F, 0.48F, 0.92F, 1.0F};
  Color accentHovered{0.25F, 0.55F, 1.0F, 1.0F};
  Color accentPressed{0.14F, 0.38F, 0.78F, 1.0F};
  Color accentForeground{1.0F, 1.0F, 1.0F, 1.0F};
  Color border{0.29F, 0.31F, 0.37F, 1.0F};
  Color focus{0.42F, 0.72F, 1.0F, 1.0F};
  Color selection{0.16F, 0.5F, 0.95F, 0.5F};
  Color disabledSurface{0.12F, 0.13F, 0.15F, 1.0F};
  Color disabledForeground{0.45F, 0.47F, 0.51F, 1.0F};
  Color error{0.88F, 0.24F, 0.27F, 1.0F};
};

/// @brief Named font sizes for body, label, title, and display text.
struct TypographyScale final {
  F32 body{14.0F};
  F32 caption{12.0F};
  F32 title{20.0F};
};

/// @brief Named spacing increments used by standard layouts.
struct SpacingScale final {
  F32 compact{4.0F};
  F32 regular{8.0F};
  F32 spacious{16.0F};
};

/// @brief Named corner radii used by standard controls.
struct RadiusScale final {
  F32 small{4.0F};
  F32 regular{8.0F};
  F32 large{14.0F};
};

/// @brief Standard control heights and pointer-target size.
struct ControlSizeScale final {
  F32 compactHeight{32.0F};
  F32 regularHeight{40.0F};
  F32 spaciousHeight{48.0F};
  F32 borderThickness{1.0F};
  F32 focusThickness{2.0F};
  F32 focusOffset{2.0F};
};

/// @brief Durations used by theme-aware transitions.
struct MotionTheme final {
  F32 fastMilliseconds{100.0F};
  F32 regularMilliseconds{180.0F};
};

/// @brief Complete palette, typography, spacing, radius, size, and motion theme.
struct Theme final {
  UInt64 revision{1};
  ColorPalette colors{};
  TypographyScale typography{};
  SpacingScale spacing{};
  RadiusScale radii{};
  ControlSizeScale controls{};
  MotionTheme motion{};
};

[[nodiscard]] inline auto MakePanelVisual(const Theme &theme)
    -> VisualProperties {
  VisualProperties result{};
  result.base.background = theme.colors.surface;
  result.base.borderColor = theme.colors.border;
  result.base.borderThickness =
      Thickness::Uniform(Dp{theme.controls.borderThickness});
  result.base.cornerRadius = CornerRadius::Uniform(Dp{theme.radii.regular});
  return result;
}

[[nodiscard]] inline auto MakeButtonVisual(const Theme &theme)
    -> VisualProperties {
  auto result = MakePanelVisual(theme);
  result.base.background = theme.colors.accent;
  result.base.foreground = theme.colors.accentForeground;
  result.states.hovered.background = theme.colors.accentHovered;
  result.states.pressed.background = theme.colors.accentPressed;
  result.states.disabled.background = theme.colors.disabledSurface;
  result.states.disabled.foreground = theme.colors.disabledForeground;
  result.focus = FocusVisual{
      .color = theme.colors.focus,
      .thickness = theme.controls.focusThickness,
      .offset = theme.controls.focusOffset,
      .cornerRadius = CornerRadius::Uniform(
          Dp{theme.radii.regular + theme.controls.focusOffset}),
      .enabled = true,
  };
  return result;
}

[[nodiscard]] inline auto MakeTextFieldVisual(const Theme &theme)
    -> VisualProperties {
  auto result = MakePanelVisual(theme);
  result.base.background = theme.colors.sunkenSurface;
  result.base.foreground = theme.colors.foreground;
  result.states.hovered.borderColor = theme.colors.mutedForeground;
  result.states.focused.borderColor = theme.colors.focus;
  result.states.invalid.borderColor = theme.colors.error;
  result.states.readOnly.background = theme.colors.disabledSurface;
  result.states.disabled.background = theme.colors.disabledSurface;
  result.states.disabled.foreground = theme.colors.disabledForeground;
  result.focus = FocusVisual{
      .color = theme.colors.focus,
      .thickness = theme.controls.focusThickness,
      .offset = theme.controls.focusOffset,
      .cornerRadius = CornerRadius::Uniform(
          Dp{theme.radii.regular + theme.controls.focusOffset}),
      .enabled = true,
  };
  return result;
}

[[nodiscard]] inline auto MakeSeparatorVisual(const Theme &theme)
    -> VisualProperties {
  VisualProperties result{};
  result.base.background = theme.colors.border;
  return result;
}

[[nodiscard]] inline auto MakeLightTheme() -> Theme {
  Theme result{};
  result.revision = 1;
  result.colors = ColorPalette{
      .background = Color{0.95F, 0.96F, 0.98F, 1.0F},
      .surface = Color{1.0F, 1.0F, 1.0F, 1.0F},
      .raisedSurface = Color{1.0F, 1.0F, 1.0F, 1.0F},
      .sunkenSurface = Color{0.91F, 0.93F, 0.96F, 1.0F},
      .foreground = Color{0.08F, 0.1F, 0.14F, 1.0F},
      .mutedForeground = Color{0.34F, 0.38F, 0.45F, 1.0F},
      .accent = Color{0.08F, 0.38F, 0.82F, 1.0F},
      .accentHovered = Color{0.05F, 0.45F, 0.94F, 1.0F},
      .accentPressed = Color{0.04F, 0.3F, 0.68F, 1.0F},
      .accentForeground = Color{1.0F, 1.0F, 1.0F, 1.0F},
      .border = Color{0.7F, 0.73F, 0.78F, 1.0F},
      .focus = Color{0.0F, 0.34F, 0.82F, 1.0F},
      .selection = Color{0.1F, 0.42F, 0.9F, 0.3F},
      .disabledSurface = Color{0.88F, 0.89F, 0.91F, 1.0F},
      .disabledForeground = Color{0.49F, 0.51F, 0.55F, 1.0F},
      .error = Color{0.75F, 0.12F, 0.16F, 1.0F},
  };
  return result;
}
} // namespace NGIN::UI
