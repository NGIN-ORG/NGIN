#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

#include <vector>

namespace NGIN::UI {
/// @brief Resolved tracks for one Grid in the latest layout pass.
struct GridLayoutDiagnostics final {
  ElementId element{};
  std::vector<F32> columns{};
  std::vector<F32> rows{};
};

/// @brief Item count and extents for one resolved WrapPanel line.
struct WrapLineDiagnostics final {
  UIntSize itemCount{0};
  F32 mainExtent{0.0F};
  F32 crossExtent{0.0F};
};

/// @brief Resolved lines for one WrapPanel in the latest layout pass.
struct WrapPanelLayoutDiagnostics final {
  ElementId element{};
  WrapOrientation orientation{WrapOrientation::Horizontal};
  std::vector<WrapLineDiagnostics> lines{};
};

/// @brief Node counts and resolved desktop-layout diagnostics.
struct LayoutPassStats final {
  UIntSize measured{0};
  UIntSize arranged{0};
  std::vector<GridLayoutDiagnostics> grids{};
  std::vector<WrapPanelLayoutDiagnostics> wrapPanels{};
};

/// @brief Measures and arranges a reconciled runtime tree in logical units.
class LayoutEngine final {
public:
  explicit LayoutEngine(RuntimeTree &tree) noexcept;

  auto Perform(SizeConstraints constraints, Rect finalBounds,
               F32 scaleFactor = 1.0F) -> LayoutPassStats;
  [[nodiscard]] auto Measure(ElementHandle node, SizeConstraints constraints)
      -> Size;
  void Arrange(ElementHandle node, Rect finalBounds);

private:
  [[nodiscard]] auto MeasureLeaf(RuntimeNode &node, SizeConstraints constraints)
      -> Size;
  [[nodiscard]] auto MeasureCustom(RuntimeNode &node,
                                   SizeConstraints constraints) -> Size;
  [[nodiscard]] auto MeasureText(RuntimeNode &node, SizeConstraints constraints,
                                 const NGIN::Text::String &value) -> Size;
  [[nodiscard]] auto MeasureImage(RuntimeNode &node,
                                  SizeConstraints constraints) -> Size;
  [[nodiscard]] auto MeasureContainer(RuntimeNode &node,
                                      SizeConstraints constraints) -> Size;
  [[nodiscard]] auto MeasureGrid(RuntimeNode &node, SizeConstraints constraints)
      -> Size;
  [[nodiscard]] auto MeasureWrapPanel(RuntimeNode &node,
                                      SizeConstraints constraints) -> Size;
  [[nodiscard]] auto MeasureCanvas(RuntimeNode &node,
                                   SizeConstraints constraints) -> Size;
  void ArrangeChildren(RuntimeNode &node);
  void ArrangeGrid(RuntimeNode &node, Rect content);
  void ArrangeWrapPanel(RuntimeNode &node, Rect content);
  void ArrangeCanvas(RuntimeNode &node, Rect content);
  [[nodiscard]] auto ResolveChildWidth(const RuntimeNode &child,
                                       F32 available) const noexcept -> F32;
  [[nodiscard]] auto ResolveChildHeight(const RuntimeNode &child,
                                        F32 available) const noexcept -> F32;

  RuntimeTree &m_tree;
  UInt64 m_revision{0};
  F32 m_scaleFactor{1.0F};
  LayoutPassStats m_stats{};
};
} // namespace NGIN::UI
