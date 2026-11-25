#pragma once

#include <NGIN/UI/DisplayList.hpp>
#include <unordered_map>

namespace NGIN::UI::Detail {
/// Stores balanced slices of the preceding paint pass for mounted component
/// roots.
struct RetainedDisplayList final {
  struct Slice final {
    UInt64 compositionRevision{};
    UInt64 layoutRevision{};
    UIntSize start{};
    UIntSize end{};
  };

  DisplayList commands{};
  std::unordered_map<UInt64, Slice> slices{};
  UIntSize reusedSubtrees{};

  // The command cache may outlive the layout that supplied a glyph. Keep weak
  // leases per command here; the published packet owns the strong leases.
  std::unordered_map<UIntSize,
                     std::vector<std::weak_ptr<const GlyphAtlasLease>>>
      glyphLeases{};
  void ReleaseGlyphLeases();
};

auto RecordDisplayList(const RuntimeTree &tree,
                       const RetainedDisplayList *previous)
    -> RetainedDisplayList;
} // namespace NGIN::UI::Detail
