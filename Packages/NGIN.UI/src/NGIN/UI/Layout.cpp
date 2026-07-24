#include <NGIN/UI/Layout.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto ClampDimension(const F32 value, const F32 minimum,
                                  const F32 maximum) noexcept -> F32 {
  return std::clamp(std::max(0.0F, value), std::max(0.0F, minimum),
                    std::max(minimum, maximum));
}

[[nodiscard]] auto InnerWidth(const F32 width,
                              const Thickness &padding) noexcept -> F32 {
  return std::max(0.0F, width - padding.left - padding.right);
}

[[nodiscard]] auto InnerHeight(const F32 height,
                               const Thickness &padding) noexcept -> F32 {
  return std::max(0.0F, height - padding.top - padding.bottom);
}

[[nodiscard]] auto AlignmentOffset(const F32 available, const F32 extent,
                                   const HorizontalAlignment alignment) noexcept
    -> F32 {
  switch (alignment) {
  case HorizontalAlignment::Center:
    return (available - extent) * 0.5F;
  case HorizontalAlignment::End:
    return available - extent;
  default:
    return 0.0F;
  }
}

[[nodiscard]] auto AlignmentOffset(const F32 available, const F32 extent,
                                   const VerticalAlignment alignment) noexcept
    -> F32 {
  switch (alignment) {
  case VerticalAlignment::Center:
    return (available - extent) * 0.5F;
  case VerticalAlignment::End:
    return available - extent;
  default:
    return 0.0F;
  }
}
} // namespace

LayoutEngine::LayoutEngine(RuntimeTree &tree) noexcept : m_tree(tree) {}

auto LayoutEngine::Perform(const SizeConstraints constraints,
                           const Rect finalBounds) -> LayoutPassStats {
  m_stats = {};
  ++m_revision;
  static_cast<void>(Measure(m_tree.Root(), constraints));
  Arrange(m_tree.Root(), finalBounds);
  return m_stats;
}

auto LayoutEngine::Measure(const ElementHandle handle,
                           SizeConstraints constraints) -> Size {
  auto *node = m_tree.Get(handle);
  if (node == nullptr) {
    return {};
  }

  constraints.minimum.width = std::max(
      constraints.minimum.width, node->properties.layout.minimumSize.width);
  constraints.minimum.height = std::max(
      constraints.minimum.height, node->properties.layout.minimumSize.height);
  constraints.maximum.width = std::min(
      constraints.maximum.width, node->properties.layout.maximumSize.width);
  constraints.maximum.height = std::min(
      constraints.maximum.height, node->properties.layout.maximumSize.height);
  constraints.maximum.width =
      std::max(constraints.maximum.width, constraints.minimum.width);
  constraints.maximum.height =
      std::max(constraints.maximum.height, constraints.minimum.height);

  auto measured = node->children.empty() || node->type == ElementType::Spacer
                      ? MeasureLeaf(*node, constraints)
                      : MeasureContainer(*node, constraints);
  measured = constraints.Constrain(measured);
  node = m_tree.Get(handle);
  node->measuredSize = measured;
  node->layoutRevision = m_revision;
  ++m_stats.measured;
  return measured;
}

auto LayoutEngine::MeasureLeaf(const RuntimeNode &node,
                               const SizeConstraints constraints) const
    -> Size {
  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               node.properties.layout.minimumSize.width),
      std::max(node.properties.layout.preferredSize.height,
               node.properties.layout.minimumSize.height),
  });
}

auto LayoutEngine::MeasureContainer(RuntimeNode &node,
                                    const SizeConstraints constraints) -> Size {
  const auto padding = node.properties.layout.padding;
  const auto horizontalPadding = padding.left + padding.right;
  const auto verticalPadding = padding.top + padding.bottom;
  const auto availableWidth =
      std::isfinite(constraints.maximum.width)
          ? InnerWidth(constraints.maximum.width, padding)
          : constraints.maximum.width;
  const auto availableHeight =
      std::isfinite(constraints.maximum.height)
          ? InnerHeight(constraints.maximum.height, padding)
          : constraints.maximum.height;

  F32 contentWidth = 0.0F;
  F32 contentHeight = 0.0F;
  const auto gapCount = node.children.empty()
                            ? 0U
                            : static_cast<UIntSize>(node.children.size() - 1);

  for (const auto childHandle : node.children) {
    SizeConstraints childConstraints{};
    childConstraints.maximum = Size{availableWidth, availableHeight};
    if (node.type == ElementType::ScrollView) {
      if (node.properties.scroll.horizontal) {
        childConstraints.maximum.width = std::numeric_limits<F32>::infinity();
      }
      if (node.properties.scroll.vertical) {
        childConstraints.maximum.height = std::numeric_limits<F32>::infinity();
      }
    } else if (node.type == ElementType::Column) {
      childConstraints.maximum.height = std::numeric_limits<F32>::infinity();
    } else if (node.type == ElementType::Row) {
      childConstraints.maximum.width = std::numeric_limits<F32>::infinity();
    }

    const auto childSize = Measure(childHandle, childConstraints);
    switch (node.type) {
    case ElementType::Column:
      contentWidth = std::max(contentWidth, childSize.width);
      contentHeight += childSize.height;
      break;
    case ElementType::Row:
      contentWidth += childSize.width;
      contentHeight = std::max(contentHeight, childSize.height);
      break;
    default:
      contentWidth = std::max(contentWidth, childSize.width);
      contentHeight = std::max(contentHeight, childSize.height);
      break;
    }
  }

  if (node.type == ElementType::Column) {
    contentHeight += node.properties.layout.gap * static_cast<F32>(gapCount);
  } else if (node.type == ElementType::Row) {
    contentWidth += node.properties.layout.gap * static_cast<F32>(gapCount);
  }

  return constraints.Constrain(Size{
      std::max(node.properties.layout.preferredSize.width,
               contentWidth + horizontalPadding),
      std::max(node.properties.layout.preferredSize.height,
               contentHeight + verticalPadding),
  });
}

void LayoutEngine::Arrange(const ElementHandle handle, const Rect finalBounds) {
  auto *node = m_tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  node->arrangedBounds = finalBounds;
  node->layoutRevision = m_revision;
  ++m_stats.arranged;
  ArrangeChildren(*node);
}

void LayoutEngine::ArrangeChildren(RuntimeNode &node) {
  const auto padding = node.properties.layout.padding;
  const Rect content{
      node.arrangedBounds.x + padding.left,
      node.arrangedBounds.y + padding.top,
      InnerWidth(node.arrangedBounds.width, padding),
      InnerHeight(node.arrangedBounds.height, padding),
  };
  F32 cursorX = content.x;
  F32 cursorY = content.y;

  if (node.type == ElementType::ScrollView) {
    Size contentSize{};
    for (const auto childHandle : node.children) {
      if (const auto *child = m_tree.Get(childHandle); child != nullptr) {
        contentSize.width =
            std::max(contentSize.width, child->measuredSize.width);
        contentSize.height =
            std::max(contentSize.height, child->measuredSize.height);
      }
    }
    if (!node.properties.scroll.horizontal) {
      contentSize.width = content.width;
    }
    if (!node.properties.scroll.vertical) {
      contentSize.height = content.height;
    }
    node.scroll.viewportSize = Size{content.width, content.height};
    node.scroll.contentSize = contentSize;
    node.scroll.offset.x =
        std::clamp(node.scroll.offset.x, 0.0F,
                   std::max(0.0F, contentSize.width - content.width));
    node.scroll.offset.y =
        std::clamp(node.scroll.offset.y, 0.0F,
                   std::max(0.0F, contentSize.height - content.height));

    for (const auto childHandle : node.children) {
      const auto *child = m_tree.Get(childHandle);
      if (child == nullptr) {
        continue;
      }
      const auto width = node.properties.scroll.horizontal
                             ? child->measuredSize.width
                             : content.width;
      const auto height = node.properties.scroll.vertical
                              ? child->measuredSize.height
                              : content.height;
      Arrange(childHandle,
              Rect{content.x - node.scroll.offset.x,
                   content.y - node.scroll.offset.y, width, height});
    }
    return;
  }

  for (const auto childHandle : node.children) {
    const auto *child = m_tree.Get(childHandle);
    if (child == nullptr) {
      continue;
    }

    auto childWidth = ResolveChildWidth(*child, content.width);
    auto childHeight = ResolveChildHeight(*child, content.height);
    F32 x = content.x;
    F32 y = content.y;

    if (node.type == ElementType::Column) {
      childHeight = child->measuredSize.height;
      y = cursorY;
      x += AlignmentOffset(content.width, childWidth,
                           child->properties.layout.horizontalAlignment);
      cursorY += childHeight + node.properties.layout.gap;
    } else if (node.type == ElementType::Row) {
      childWidth = child->measuredSize.width;
      x = cursorX;
      y += AlignmentOffset(content.height, childHeight,
                           child->properties.layout.verticalAlignment);
      cursorX += childWidth + node.properties.layout.gap;
    } else {
      x += AlignmentOffset(content.width, childWidth,
                           child->properties.layout.horizontalAlignment);
      y += AlignmentOffset(content.height, childHeight,
                           child->properties.layout.verticalAlignment);
    }

    Arrange(childHandle, Rect{x, y, childWidth, childHeight});
  }
}

auto LayoutEngine::ResolveChildWidth(const RuntimeNode &child,
                                     const F32 available) const noexcept
    -> F32 {
  if (child.properties.layout.horizontalAlignment ==
      HorizontalAlignment::Stretch) {
    return ClampDimension(available, child.properties.layout.minimumSize.width,
                          child.properties.layout.maximumSize.width);
  }
  return std::min(available, child.measuredSize.width);
}

auto LayoutEngine::ResolveChildHeight(const RuntimeNode &child,
                                      const F32 available) const noexcept
    -> F32 {
  if (child.properties.layout.verticalAlignment == VerticalAlignment::Stretch) {
    return ClampDimension(available, child.properties.layout.minimumSize.height,
                          child.properties.layout.maximumSize.height);
  }
  return std::min(available, child.measuredSize.height);
}
} // namespace NGIN::UI
