#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Style.hpp>

namespace NGIN::UI {
struct ColorPalette final {
  Color background{0.08F, 0.09F, 0.11F, 1.0F};
  Color surface{0.14F, 0.15F, 0.18F, 1.0F};
  Color foreground{0.94F, 0.95F, 0.97F, 1.0F};
  Color mutedForeground{0.65F, 0.68F, 0.72F, 1.0F};
  Color accent{0.20F, 0.48F, 0.92F, 1.0F};
  Color error{0.88F, 0.24F, 0.27F, 1.0F};
};

struct TypographyScale final {
  F32 body{14.0F};
  F32 caption{12.0F};
  F32 title{20.0F};
};

struct SpacingScale final {
  F32 compact{4.0F};
  F32 regular{8.0F};
  F32 spacious{16.0F};
};

struct MotionTheme final {
  F32 fastMilliseconds{100.0F};
  F32 regularMilliseconds{180.0F};
};

struct Theme final {
  UInt64 revision{1};
  ColorPalette colors{};
  TypographyScale typography{};
  SpacingScale spacing{};
  MotionTheme motion{};
};
} // namespace NGIN::UI
