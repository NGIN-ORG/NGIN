#include <NGIN/UI/RuntimeTree.hpp>

#include "MotionInternal.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto ContextFor(RuntimeNode &node, const F32 scaleFactor)
    -> CustomElementContext {
  return CustomElementContext{
      *node.custom.state,
      node.id,
      node.arrangedBounds,
      CustomInteractionState{
          .hovered = node.interaction.hovered,
          .pressed =
              node.interaction.pressed || node.interaction.keyboardPressed,
          .focused = node.interaction.focused,
          .enabled = node.properties.interaction.enabled,
      },
      scaleFactor,
      {},
      node.motion.get(),
  };
}

void ReportCustomError(const RuntimeNode &node, const UIError &error) noexcept {
  if (!node.properties.custom.onError) {
    return;
  }
  try {
    node.properties.custom.onError(error);
  } catch (...) {
  }
}

[[nodiscard]] auto MergeSemantics(const SemanticProperties &provided,
                                  const SemanticProperties &authored)
    -> SemanticProperties {
  auto result = provided;
  if (authored.role != SemanticRole::None) {
    result.role = authored.role;
  }
  if (!authored.label.Empty()) {
    result.label = authored.label;
  }
  if (!authored.value.Empty()) {
    result.value = authored.value;
  }
  if (!authored.description.Empty()) {
    result.description = authored.description;
  }
  result.states |= authored.states;
  if (authored.actions != SemanticActionFlags::None) {
    result.actions = authored.actions;
  }
  if (authored.collectionItem) {
    result.collectionItem = authored.collectionItem;
  }
  if (authored.live != SemanticLiveSetting::Off) {
    result.live = authored.live;
  }
  result.hidden = result.hidden || authored.hidden;
  return result;
}
} // namespace

RuntimeTree::RuntimeTree() {
  m_root =
      CreateNode(ElementType::Root, NGIN::Text::String{}, NodeProperties{}, {});
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

auto RuntimeTree::FindById(const ElementId id) const noexcept -> ElementHandle {
  if (!id.IsValid()) {
    return {};
  }
  for (const auto &slot : m_slots) {
    if (slot.occupied && slot.node.id == id) {
      return slot.node.handle;
    }
  }
  return {};
}

auto RuntimeTree::ResourcesFor(ElementHandle handle) const noexcept
    -> std::shared_ptr<const ResourceScope> {
  while (const auto *node = Get(handle)) {
    if (node->properties.resources) {
      return node->properties.resources;
    }
    handle = node->parent;
  }
  return {};
}

auto RuntimeTree::FindBySemanticIdentifier(
    const NGIN::Text::String &identifier) const noexcept -> ElementHandle {
  if (identifier.Empty()) {
    return {};
  }
  for (const auto &slot : m_slots) {
    if (slot.occupied &&
        slot.node.properties.semantics.identifier == identifier) {
      return slot.node.handle;
    }
  }
  return {};
}

auto RuntimeTree::LiveCount() const noexcept -> UIntSize { return m_liveCount; }

auto RuntimeTree::CreateNode(const ElementType type,
                             const NGIN::Text::String &key,
                             const NodeProperties &properties,
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
      .properties = properties,
  };
  SynchronizeTextField(slot.node);
  SynchronizeCustom(slot.node);
  ++m_liveCount;
  return handle;
}

void RuntimeTree::SynchronizeTextField(RuntimeNode &node) {
  if (node.type != ElementType::TextField &&
      node.type != ElementType::TextArea) {
    node.textField = {};
    return;
  }

  const auto &properties = node.properties.textField;
  const auto report = [&properties](const UIError &error) {
    if (properties.onError) {
      properties.onError(error);
    }
  };
  if (properties.graphemeSegmenter == nullptr ||
      !properties.value.IsReadable()) {
    node.textField = {};
    report(MakeUIError(
        UIErrorCode::InvalidArgument,
        "Text editing controls require a value binding and grapheme "
        "segmenter",
        "NGIN.UI", "RuntimeTree::SynchronizeTextField"));
    return;
  }

  if (!node.textField.editing ||
      node.textField.graphemeSegmenter != properties.graphemeSegmenter) {
    auto editing =
        std::make_shared<TextEditingBuffer>(*properties.graphemeSegmenter);
    auto reset = editing->Reset(properties.value.Get());
    if (!reset) {
      node.textField = {};
      report(reset.Error());
      return;
    }
    node.textField.editing = std::move(editing);
    node.textField.graphemeSegmenter = properties.graphemeSegmenter;
    return;
  }

  if (!node.textField.editing->HasComposition() &&
      node.textField.editing->Value() != properties.value.Get()) {
    auto reset = node.textField.editing->Reset(properties.value.Get());
    if (!reset) {
      report(reset.Error());
    }
  }
}

void RuntimeTree::SynchronizeCustom(RuntimeNode &node, const F32 scaleFactor) {
  if (node.type != ElementType::CustomElement) {
    node.custom = {};
    return;
  }

  if (!node.custom.state) {
    node.custom.state = std::make_shared<CustomStateStore>();
  }
  node.custom.scaleFactor = scaleFactor > 0.0F ? scaleFactor : 1.0F;
  node.custom.semantics = node.properties.semantics;
  if (!node.properties.custom.element) {
    ReportCustomError(node,
                      MakeUIError(UIErrorCode::InvalidArgument,
                                  "Custom element requires an implementation",
                                  "NGIN.UI", "RuntimeTree::SynchronizeCustom"));
    return;
  }

  try {
    auto context = ContextFor(node, node.custom.scaleFactor);
    auto described = node.properties.custom.element->Semantics(context);
    if (!described) {
      ReportCustomError(node, described.Error());
      return;
    }
    node.custom.semantics =
        MergeSemantics(described.Value(), node.properties.semantics);
  } catch (const std::bad_alloc &) {
    ReportCustomError(node,
                      MakeUIError(UIErrorCode::OutOfMemory,
                                  "Custom semantics allocation failed",
                                  "NGIN.UI", "ICustomElement::Semantics"));
  } catch (...) {
    ReportCustomError(
        node, MakeUIError(UIErrorCode::InvalidState,
                          "Custom semantics callback threw an exception",
                          "NGIN.UI", "ICustomElement::Semantics"));
  }
}

void RuntimeTree::UnmountCustom(RuntimeNode &node) noexcept {
  if (node.type != ElementType::CustomElement || !node.custom.state ||
      !node.properties.custom.element) {
    return;
  }
  auto context = ContextFor(node, node.custom.scaleFactor);
  node.properties.custom.element->Unmounted(context);
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

  UnmountCustom(slot.node);
  Detail::UnmountMotionNode(slot.node);
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
    node->properties = declaration.properties;
    m_tree.SynchronizeTextField(*node);
    m_tree.SynchronizeCustom(*node);
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
  const auto handle = m_tree.CreateNode(declaration.type, declaration.key,
                                        declaration.properties, parent);
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
