#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::UI {
struct Color final {
  F32 red{0.0F};
  F32 green{0.0F};
  F32 blue{0.0F};
  F32 alpha{1.0F};

  [[nodiscard]] constexpr auto
  operator<=>(const Color &) const noexcept = default;
};
} // namespace NGIN::UI
