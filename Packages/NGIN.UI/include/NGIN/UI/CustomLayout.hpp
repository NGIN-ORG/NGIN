#pragma once

#include <NGIN/UI/Geometry.hpp>

namespace NGIN::UI {
/// @brief Retained layout engine that invokes custom policies.
class LayoutEngine;
/// @brief Runtime-owned element with measured and arranged geometry.
struct RuntimeNode;

/// @brief Scoped access to the direct children of a custom container during
/// measurement.
class MeasureChildren final {
public:
  MeasureChildren(const MeasureChildren &) = delete;
  auto operator=(const MeasureChildren &) -> MeasureChildren & = delete;

  [[nodiscard]] auto Count() const noexcept -> UIntSize;
  [[nodiscard]] auto Measure(UIntSize index, SizeConstraints constraints)
      -> Size;

private:
  friend class LayoutEngine;
  MeasureChildren(LayoutEngine &engine, const RuntimeNode &parent) noexcept
      : m_engine(engine), m_parent(parent) {}
  LayoutEngine &m_engine;
  const RuntimeNode &m_parent;
};

/// @brief Places direct children in coordinates relative to the container's
/// padding.
class ArrangeChildren final {
public:
  ArrangeChildren(const ArrangeChildren &) = delete;
  auto operator=(const ArrangeChildren &) -> ArrangeChildren & = delete;

  [[nodiscard]] auto Count() const noexcept -> UIntSize;
  [[nodiscard]] auto DesiredSize(UIntSize index) const -> Size;
  void Place(UIntSize index, Rect bounds);

private:
  friend class LayoutEngine;
  ArrangeChildren(LayoutEngine &engine, const RuntimeNode &parent,
                  Point origin) noexcept
      : m_engine(engine), m_parent(parent), m_origin(origin) {}
  LayoutEngine &m_engine;
  const RuntimeNode &m_parent;
  Point m_origin;
};

/// @brief Supplies the content measurement and placement algorithm of a Custom
/// container. Measure and place each direct child, including collapsed slots.
/// Contexts are borrowed for the synchronous call. The runtime owns the nodes.
class ILayoutPolicy {
public:
  virtual ~ILayoutPolicy() = default;
  [[nodiscard]] virtual auto Measure(MeasureChildren &children,
                                     SizeConstraints available) -> Size = 0;
  virtual void Arrange(ArrangeChildren &children, Size available) = 0;
};
} // namespace NGIN::UI
