#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Geometry.hpp>

#include <optional>

namespace NGIN::UI {
struct Color final {
  F32 red{0.0F};
  F32 green{0.0F};
  F32 blue{0.0F};
  F32 alpha{1.0F};

  [[nodiscard]] constexpr auto
  operator<=>(const Color &) const noexcept = default;
};

enum class VisualStateFlags : UInt16 {
  None = 0,
  Hovered = 1U << 0U,
  Pressed = 1U << 1U,
  Focused = 1U << 2U,
  Disabled = 1U << 3U,
  Selected = 1U << 4U,
  Invalid = 1U << 5U,
  ReadOnly = 1U << 6U,
};

[[nodiscard]] constexpr auto operator|(const VisualStateFlags left,
                                       const VisualStateFlags right) noexcept
    -> VisualStateFlags {
  return static_cast<VisualStateFlags>(static_cast<UInt16>(left) |
                                       static_cast<UInt16>(right));
}

constexpr auto operator|=(VisualStateFlags &left,
                          const VisualStateFlags right) noexcept
    -> VisualStateFlags & {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr auto
HasVisualState(const VisualStateFlags value,
               const VisualStateFlags state) noexcept -> bool {
  return (static_cast<UInt16>(value) & static_cast<UInt16>(state)) != 0;
}

struct VisualStyle final {
  std::optional<Color> background{};
  std::optional<Color> foreground{};
  std::optional<Color> borderColor{};
  Thickness borderThickness{};
  CornerRadius cornerRadius{};
};

struct VisualStylePatch final {
  std::optional<Color> background{};
  std::optional<Color> foreground{};
  std::optional<Color> borderColor{};
  std::optional<Thickness> borderThickness{};
  std::optional<CornerRadius> cornerRadius{};
};

struct VisualStateStyles final {
  VisualStylePatch hovered{};
  VisualStylePatch pressed{};
  VisualStylePatch focused{};
  VisualStylePatch disabled{};
  VisualStylePatch selected{};
  VisualStylePatch invalid{};
  VisualStylePatch readOnly{};
};

struct FocusVisual final {
  std::optional<Color> color{};
  F32 thickness{2.0F};
  F32 offset{2.0F};
  CornerRadius cornerRadius{};
  bool enabled{false};
};

struct VisualProperties final {
  VisualStyle base{};
  VisualStateStyles states{};
  FocusVisual focus{};
  VisualStateFlags state{VisualStateFlags::None};
};

inline void ApplyVisualStylePatch(VisualStyle &style,
                                  const VisualStylePatch &patch) {
  if (patch.background) {
    style.background = patch.background;
  }
  if (patch.foreground) {
    style.foreground = patch.foreground;
  }
  if (patch.borderColor) {
    style.borderColor = patch.borderColor;
  }
  if (patch.borderThickness) {
    style.borderThickness = *patch.borderThickness;
  }
  if (patch.cornerRadius) {
    style.cornerRadius = *patch.cornerRadius;
  }
}

[[nodiscard]] inline auto ResolveVisualStyle(const VisualProperties &properties,
                                             const VisualStateFlags state)
    -> VisualStyle {
  auto result = properties.base;
  const auto apply = [&](const VisualStateFlags flag,
                         const VisualStylePatch &patch) {
    if (HasVisualState(state, flag)) {
      ApplyVisualStylePatch(result, patch);
    }
  };

  apply(VisualStateFlags::ReadOnly, properties.states.readOnly);
  apply(VisualStateFlags::Selected, properties.states.selected);
  apply(VisualStateFlags::Invalid, properties.states.invalid);
  apply(VisualStateFlags::Hovered, properties.states.hovered);
  apply(VisualStateFlags::Pressed, properties.states.pressed);
  apply(VisualStateFlags::Focused, properties.states.focused);
  apply(VisualStateFlags::Disabled, properties.states.disabled);
  return result;
}
} // namespace NGIN::UI
