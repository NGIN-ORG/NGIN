#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::UI {
/// @brief Natural, left-to-right, or right-to-left text flow.
enum class TextDirection : UInt8 {
  Automatic,
  LeftToRight,
  RightToLeft,
};
} // namespace NGIN::UI
