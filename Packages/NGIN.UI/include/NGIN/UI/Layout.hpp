#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

namespace NGIN::UI {
struct LayoutPassStats final {
  UIntSize measured{0};
  UIntSize arranged{0};
};

class LayoutEngine final {
public:
  explicit LayoutEngine(RuntimeTree &tree) noexcept;

  auto Perform(SizeConstraints constraints, Rect finalBounds)
      -> LayoutPassStats;
  [[nodiscard]] auto Measure(ElementHandle node, SizeConstraints constraints)
      -> Size;
  void Arrange(ElementHandle node, Rect finalBounds);

private:
  [[nodiscard]] auto MeasureLeaf(const RuntimeNode &node,
                                 SizeConstraints constraints) const -> Size;
  [[nodiscard]] auto MeasureContainer(RuntimeNode &node,
                                      SizeConstraints constraints) -> Size;
  void ArrangeChildren(RuntimeNode &node);
  [[nodiscard]] auto ResolveChildWidth(const RuntimeNode &child,
                                       F32 available) const noexcept -> F32;
  [[nodiscard]] auto ResolveChildHeight(const RuntimeNode &child,
                                        F32 available) const noexcept -> F32;

  RuntimeTree &m_tree;
  UInt64 m_revision{0};
  LayoutPassStats m_stats{};
};
} // namespace NGIN::UI
