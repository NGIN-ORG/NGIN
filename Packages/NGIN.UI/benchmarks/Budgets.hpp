#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::UI::Benchmarks {
struct Budget final {
  const char *name;
  F64 maximumMedianMilliseconds;
  UInt64 maximumMedianAllocations;
};

inline constexpr Budget CompositionBudget{"composition-2000", 250.0, 20000};
inline constexpr Budget LayoutBudget{"layout-2000", 250.0, 2000};
inline constexpr Budget TextBudget{"text-paragraph", 500.0, 20000};
// The non-virtualized baseline intentionally constructs, reconciles, and lays
// out every item. Keep enough headroom for allocator differences across the
// supported standard libraries while still catching substantial regressions.
inline constexpr Budget LargeListBudget{"large-list-10000", 2000.0, 450000};
// The virtualized path must stay independent of the 100,000-item logical
// count. This budget includes source synchronization, composition,
// reconciliation, and layout of one 720-pixel viewport.
inline constexpr Budget VirtualizedListBudget{"virtual-list-100000", 50.0,
                                              5000};
} // namespace NGIN::UI::Benchmarks
