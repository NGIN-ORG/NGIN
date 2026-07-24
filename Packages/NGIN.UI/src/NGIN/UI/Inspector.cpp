#include <NGIN/UI/Inspector.hpp>

#include <NGIN/UI/Application.hpp>

#include <algorithm>

namespace NGIN::UI {
namespace {
void AppendNodeSnapshot(const RuntimeTree &tree, const ElementHandle handle,
                        const UIntSize depth, InspectorSnapshot &snapshot) {
  const auto *node = tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  snapshot.nodes.push_back(InspectorNodeSnapshot{
      .handle = node->handle,
      .parent = node->parent,
      .id = node->id,
      .type = node->type,
      .key = node->key,
      .depth = depth,
      .measuredSize = node->measuredSize,
      .arrangedBounds = node->arrangedBounds,
      .interaction = node->interaction,
      .scroll = node->scroll,
      .popup = node->popup,
      .enabled = node->properties.interaction.enabled,
      .hitTestVisible = node->properties.interaction.hitTestVisible,
      .focusable = node->properties.interaction.focusable,
      .compositionRevision = node->compositionRevision,
      .layoutRevision = node->layoutRevision,
  });
  for (const auto child : node->children) {
    AppendNodeSnapshot(tree, child, depth + 1, snapshot);
  }
}

void AppendOverlayCommands(const RuntimeTree &tree, const ElementHandle handle,
                           const InspectorOverlayOptions &options,
                           DisplayList &result) {
  const auto *node = tree.Get(handle);
  if (node == nullptr) {
    return;
  }
  const auto bounds = node->arrangedBounds;
  const auto hasBounds = bounds.width > 0.0F && bounds.height > 0.0F;
  const auto thickness = std::max(0.5F, options.strokeThickness);
  if (hasBounds && options.showLayoutBounds) {
    result.emplace_back(StrokeRect{
        .rect = bounds,
        .thickness = thickness,
        .color = options.layoutColor,
    });
  }
  if (hasBounds && options.showHitTestBounds &&
      node->properties.interaction.hitTestVisible) {
    result.emplace_back(StrokeRect{
        .rect = bounds,
        .thickness = thickness * 2.0F,
        .color = options.hitTestColor,
    });
  }
  if (hasBounds && options.showFocus && node->interaction.focused) {
    result.emplace_back(StrokeRect{
        .rect = bounds,
        .thickness = thickness * 3.0F,
        .color = options.focusColor,
    });
  }
  if (hasBounds && handle == options.selected) {
    result.emplace_back(StrokeRect{
        .rect = bounds,
        .thickness = thickness * 3.0F,
        .color = options.selectedColor,
    });
  }
  for (const auto child : node->children) {
    AppendOverlayCommands(tree, child, options, result);
  }
}
} // namespace

auto CaptureInspectorSnapshot(const Window &window) -> InspectorSnapshot {
  InspectorSnapshot snapshot{
      .windowId = window.Id(),
      .pixelExtent = window.PixelExtent(),
      .scaleFactor = window.ScaleFactor(),
      .diagnostics = window.Diagnostics(),
      .semanticNodes = window.Semantics().Nodes(),
  };
  AppendNodeSnapshot(window.Tree(), window.Tree().Root(), 0, snapshot);
  return snapshot;
}

auto BuildInspectorOverlay(const RuntimeTree &tree,
                           const InspectorOverlayOptions &options)
    -> DisplayList {
  DisplayList result;
  if (options.enabled) {
    AppendOverlayCommands(tree, tree.Root(), options, result);
  }
  return result;
}
} // namespace NGIN::UI
