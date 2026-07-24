#include <NGIN/UI/RuntimeTree.hpp>

#include <algorithm>
#include <utility>

namespace NGIN::UI {
RuntimeTree::RuntimeTree() {
  m_root = CreateNode(ElementType::Root, NGIN::Text::String{}, {});
}

auto RuntimeTree::Root() const noexcept -> ElementHandle { return m_root; }

auto RuntimeTree::IsAlive(const ElementHandle handle) const noexcept -> bool {
  return handle.IsValid() && handle.index < m_slots.size() &&
         m_slots[handle.index].occupied &&
         m_slots[handle.index].generation == handle.generation;
}

auto RuntimeTree::Get(const ElementHandle handle) noexcept -> RuntimeNode * {
  return IsAlive(handle) ? &m_slots[handle.index].node : nullptr;
}

auto RuntimeTree::Get(const ElementHandle handle) const noexcept
    -> const RuntimeNode * {
  return IsAlive(handle) ? &m_slots[handle.index].node : nullptr;
}

auto RuntimeTree::LiveCount() const noexcept -> UIntSize { return m_liveCount; }

auto RuntimeTree::CreateNode(const ElementType type,
                             const NGIN::Text::String &key,
                             const ElementHandle parent) -> ElementHandle {
  UInt32 index = 0;
  if (m_freeSlots.empty()) {
    index = static_cast<UInt32>(m_slots.size());
    m_slots.emplace_back();
  } else {
    index = m_freeSlots.back();
    m_freeSlots.pop_back();
  }

  auto &slot = m_slots[index];
  slot.occupied = true;
  const ElementHandle handle{index, slot.generation};
  slot.node = RuntimeNode{
      .handle = handle,
      .id = ElementId{m_nextElementId++},
      .parent = parent,
      .type = type,
      .key = key,
  };
  ++m_liveCount;
  return handle;
}

auto RuntimeTree::DestroySubtree(const ElementHandle handle) noexcept
    -> UIntSize {
  if (!IsAlive(handle) || handle == m_root) {
    return 0;
  }

  auto &slot = m_slots[handle.index];
  const auto children = slot.node.children;
  UIntSize removed = 1;
  for (const auto child : children) {
    removed += DestroySubtree(child);
  }

  if (auto *parent = Get(slot.node.parent); parent != nullptr) {
    std::erase(parent->children, handle);
  }

  slot.node = RuntimeNode{};
  slot.occupied = false;
  ++slot.generation;
  if (slot.generation == 0) {
    slot.generation = 1;
  }
  m_freeSlots.push_back(handle.index);
  --m_liveCount;
  return removed;
}

Reconciler::Reconciler(RuntimeTree &tree) noexcept : m_tree(tree) {}

auto Reconciler::Reconcile(
    const std::span<const ElementDeclaration> declarations) -> ReconcileStats {
  ReconcileStats stats{};
  ++m_revision;
  ReconcileChildren(m_tree.Root(), declarations, stats);
  return stats;
}

auto Reconciler::ReconcileChildren(
    const ElementHandle parent,
    const std::span<const ElementDeclaration> declarations,
    ReconcileStats &stats) -> void {
  auto *parentNode = m_tree.Get(parent);
  if (parentNode == nullptr) {
    return;
  }

  const auto previousChildren = parentNode->children;
  std::vector<bool> used(previousChildren.size(), false);
  std::vector<ElementHandle> matches(declarations.size());

  for (UIntSize declarationIndex = 0; declarationIndex < declarations.size();
       ++declarationIndex) {
    const auto &declaration = declarations[declarationIndex];

    if (declaration.IsKeyed()) {
      for (UIntSize previousIndex = 0; previousIndex < previousChildren.size();
           ++previousIndex) {
        if (used[previousIndex]) {
          continue;
        }
        const auto *candidate = m_tree.Get(previousChildren[previousIndex]);
        if (candidate != nullptr && candidate->IsKeyed() &&
            candidate->type == declaration.type &&
            candidate->key == declaration.key) {
          matches[declarationIndex] = candidate->handle;
          used[previousIndex] = true;
          break;
        }
      }
    } else if (declarationIndex < previousChildren.size()) {
      const auto *candidate = m_tree.Get(previousChildren[declarationIndex]);
      if (candidate != nullptr && !candidate->IsKeyed() &&
          candidate->type == declaration.type) {
        matches[declarationIndex] = candidate->handle;
        used[declarationIndex] = true;
      }
    }
  }

  for (UIntSize index = 0; index < previousChildren.size(); ++index) {
    if (!used[index]) {
      stats.removed += m_tree.DestroySubtree(previousChildren[index]);
    }
  }

  std::vector<ElementHandle> nextChildren;
  nextChildren.reserve(declarations.size());
  for (UIntSize declarationIndex = 0; declarationIndex < declarations.size();
       ++declarationIndex) {
    const auto &declaration = declarations[declarationIndex];
    const auto matched = matches[declarationIndex];
    if (!matched) {
      nextChildren.push_back(MaterializeSubtree(parent, declaration, stats));
      continue;
    }

    auto *node = m_tree.Get(matched);
    node->compositionRevision = m_revision;
    nextChildren.push_back(matched);
    ++stats.preserved;
    ReconcileChildren(matched, declaration.children, stats);
  }

  parentNode = m_tree.Get(parent);
  parentNode->children = std::move(nextChildren);
}

auto Reconciler::MaterializeSubtree(const ElementHandle parent,
                                    const ElementDeclaration &declaration,
                                    ReconcileStats &stats) -> ElementHandle {
  const auto handle =
      m_tree.CreateNode(declaration.type, declaration.key, parent);
  auto *node = m_tree.Get(handle);
  node->compositionRevision = m_revision;
  node->children.reserve(declaration.children.size());
  ++stats.created;

  for (const auto &child : declaration.children) {
    const auto childHandle = MaterializeSubtree(handle, child, stats);
    m_tree.Get(handle)->children.push_back(childHandle);
  }
  return handle;
}
} // namespace NGIN::UI
