#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::UI {
enum class InvalidationKind : UInt8 {
  None = 0,
  Compose = 1U << 0U,
  Measure = 1U << 1U,
  Arrange = 1U << 2U,
  Paint = 1U << 3U,
  Semantics = 1U << 4U,
  All = (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U) | (1U << 4U),
};

[[nodiscard]] constexpr auto operator|(const InvalidationKind left,
                                       const InvalidationKind right) noexcept
    -> InvalidationKind {
  return static_cast<InvalidationKind>(static_cast<UInt8>(left) |
                                       static_cast<UInt8>(right));
}

constexpr auto operator|=(InvalidationKind &left,
                          const InvalidationKind right) noexcept
    -> InvalidationKind & {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr auto
HasInvalidation(const InvalidationKind value,
                const InvalidationKind flag) noexcept -> bool {
  return (static_cast<UInt8>(value) & static_cast<UInt8>(flag)) != 0;
}
} // namespace NGIN::UI
