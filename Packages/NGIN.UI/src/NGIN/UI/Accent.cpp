#include <NGIN/UI/Accent.hpp>

#include <algorithm>
#include <cmath>

namespace NGIN::UI {
namespace {
constexpr Color Black{0, 0, 0, 1};
constexpr Color White{1, 1, 1, 1};

auto Mix(Color from, Color to, F32 amount) noexcept -> Color {
  return {std::lerp(from.red, to.red, amount),
          std::lerp(from.green, to.green, amount),
          std::lerp(from.blue, to.blue, amount), 1};
}

// Theme RGB values map to the display's SDR channel values. Evaluate contrast
// in linear light so saturated accents are weighted by perceived brightness.
auto ToLinear(F32 value) noexcept -> F32 {
  return value <= 0.04045F ? value / 12.92F
                           : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

auto ToDisplay(F32 value) noexcept -> F32 {
  return value <= 0.0031308F ? value * 12.92F
                             : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

auto Luminance(Color color) noexcept -> F32 {
  return 0.2126F * ToLinear(color.red) + 0.7152F * ToLinear(color.green) +
         0.0722F * ToLinear(color.blue);
}

auto Contrast(F32 first, F32 second) noexcept -> F32 {
  return (std::max(first, second) + 0.05F) / (std::min(first, second) + 0.05F);
}

auto FocusColor(const ColorPalette &palette, Color accent,
                F32 luminance) noexcept -> Color {
  const auto [darkest, lightest] = std::minmax(
      {Luminance(palette.surface), Luminance(palette.background),
       Luminance(palette.sunkenSurface), Luminance(palette.raisedSurface)});
  const bool darkRing = Contrast(darkest, 0) >= Contrast(lightest, 1);
  // Reserve contrast for the selected background, which uses a tint of the
  // ring color. Solve the linear-light mix directly to retain the accent hue.
  const auto target = darkRing ? std::max(0.0F, (darkest + 0.05F) / 7 - 0.05F)
                               : std::min(1.0F, 7 * (lightest + 0.05F) - 0.05F);
  if ((darkRing && luminance <= target) || (!darkRing && luminance >= target))
    return accent;
  const F32 end = darkRing ? 0.0F : 1.0F;
  const F32 amount = (target - luminance) / (end - luminance);
  const auto channel = [&](F32 value) {
    return ToDisplay(std::lerp(ToLinear(value), end, amount));
  };
  return {channel(accent.red), channel(accent.green), channel(accent.blue), 1};
}
} // namespace

void ApplyAccent(Theme &theme, Color accent) noexcept {
  accent.alpha = 1;
  auto &palette = theme.colors;
  const auto luminance = Luminance(accent);
  const bool darkInk = Contrast(luminance, 0) >= Contrast(luminance, 1);
  palette.accent = accent;
  palette.accentForeground = darkInk ? Black : White;

  // Move toward the label color where there's enough contrast to do so.
  // At middle luminances, move away to keep the same legible label in all
  // states.
  auto toward = palette.accentForeground;
  if (Contrast(Luminance(Mix(accent, toward, 0.20F)), darkInk ? 0.0F : 1.0F) <
      4.55F)
    toward = darkInk ? White : Black;
  palette.accentHovered = Mix(accent, toward, 0.10F);
  palette.accentPressed = Mix(accent, toward, 0.20F);
  palette.focus = FocusColor(palette, accent, luminance);
  palette.selection = Mix(palette.surface, palette.focus, 0.15F);
}
} // namespace NGIN::UI
