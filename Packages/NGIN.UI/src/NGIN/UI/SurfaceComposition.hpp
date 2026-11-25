#pragma once

#include "Composer.hpp"
#include <NGIN/UI/Surface.hpp>

namespace NGIN::UI::Detail {
struct SurfaceComposition final {
  using Content = Utilities::Callable<void(Composer &)>;
  static void SetContent(Surface &surface, Content content);
};
} // namespace NGIN::UI::Detail
