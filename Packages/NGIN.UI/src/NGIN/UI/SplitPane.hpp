#pragma once

#include <NGIN/UI/Composer.hpp>

namespace NGIN::UI {
/// @brief Two side-by-side regions separated by a focusable, draggable divider.
/// The binding is the first pane's fraction of the width left after the
/// divider. Content callbacks compose synchronously and can each emit multiple
/// elements.
void SplitPane(Composer &composer, Binding<F32> fraction,
               NGIN::Utilities::Callable<void()> first,
               NGIN::Utilities::Callable<void()> second,
               const NodeProperties &properties = {},
               std::string_view key = {});
} // namespace NGIN::UI
