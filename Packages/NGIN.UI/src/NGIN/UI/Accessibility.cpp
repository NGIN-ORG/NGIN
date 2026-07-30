#include <NGIN/UI/Accessibility.hpp>

#include <algorithm>
#include <unordered_map>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto ChangedProperties(const SemanticNode &previous,
                                     const SemanticNode &current)
    -> AccessibilityPropertyFlags {
  auto result = AccessibilityPropertyFlags::None;
  if (previous.role != current.role) {
    result |= AccessibilityPropertyFlags::Role;
  }
  if (previous.label != current.label ||
      previous.labelledBy != current.labelledBy ||
      previous.labelFor != current.labelFor ||
      previous.identifier != current.identifier) {
    result |= AccessibilityPropertyFlags::Name;
  }
  if (previous.description != current.description) {
    result |= AccessibilityPropertyFlags::Description;
  }
  if (previous.value != current.value ||
      previous.password != current.password) {
    result |= AccessibilityPropertyFlags::Value;
  }
  if (previous.range != current.range) {
    result |= AccessibilityPropertyFlags::Range;
  }
  if (previous.states != current.states || previous.live != current.live) {
    result |= AccessibilityPropertyFlags::State;
  }
  if (previous.bounds != current.bounds) {
    result |= AccessibilityPropertyFlags::Bounds;
  }
  if (previous.actions != current.actions) {
    result |= AccessibilityPropertyFlags::Actions;
  }
  if (previous.collectionItem != current.collectionItem) {
    result |= AccessibilityPropertyFlags::Collection;
  }
  return result;
}

[[nodiscard]] auto Selected(const SemanticNode &node) noexcept -> bool {
  return HasSemanticState(node.states, SemanticStateFlags::Selected);
}
} // namespace

auto AccessibilitySnapshot::Find(const SemanticNodeId id) const noexcept
    -> const SemanticNode * {
  const auto found =
      std::find_if(nodes.begin(), nodes.end(),
                   [id](const SemanticNode &node) { return node.id == id; });
  return found == nodes.end() ? nullptr : &*found;
}

auto AccessibilitySnapshotDiff::Empty() const noexcept -> bool {
  return added.empty() && removed.empty() && changed.empty() &&
         selectionChanged.empty() && liveRegionChanged.empty() &&
         previousFocus == focus && !structureChanged;
}

auto DiffAccessibilitySnapshots(const AccessibilitySnapshot &previous,
                                const AccessibilitySnapshot &current)
    -> AccessibilitySnapshotDiff {
  AccessibilitySnapshotDiff result{
      .previousRevision = previous.revision,
      .revision = current.revision,
      .previousFocus = previous.focused,
      .focus = current.focused,
  };

  std::unordered_map<UInt64, const SemanticNode *> oldNodes;
  oldNodes.reserve(previous.nodes.size());
  for (const auto &node : previous.nodes) {
    oldNodes.emplace(node.id.value, &node);
  }
  std::unordered_map<UInt64, const SemanticNode *> newNodes;
  newNodes.reserve(current.nodes.size());
  for (const auto &node : current.nodes) {
    newNodes.emplace(node.id.value, &node);
    const auto found = oldNodes.find(node.id.value);
    if (found == oldNodes.end()) {
      result.added.push_back(node.id);
      result.structureChanged = true;
      if (node.live != SemanticLiveSetting::Off) {
        result.liveRegionChanged.push_back(node.id);
      }
      continue;
    }
    const auto &oldNode = *found->second;
    const auto properties = ChangedProperties(oldNode, node);
    if (properties != AccessibilityPropertyFlags::None) {
      result.changed.push_back(
          AccessibilityNodeChange{.node = node.id, .properties = properties});
    }
    if (oldNode.parent != node.parent || oldNode.children != node.children) {
      result.structureChanged = true;
    }
    if (Selected(oldNode) != Selected(node)) {
      result.selectionChanged.push_back(node.id);
    }
    if (node.live != SemanticLiveSetting::Off &&
        (oldNode.label != node.label || oldNode.value != node.value ||
         oldNode.description != node.description)) {
      result.liveRegionChanged.push_back(node.id);
    }
  }
  for (const auto &node : previous.nodes) {
    if (!newNodes.contains(node.id.value)) {
      result.removed.push_back(node.id);
      result.structureChanged = true;
    }
  }
  return result;
}
} // namespace NGIN::UI
