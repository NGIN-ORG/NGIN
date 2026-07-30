#include <NGIN/UI/Semantics.hpp>

#include <NGIN/UI/RuntimeTree.hpp>

#include <algorithm>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto DefaultRole(const ElementType type) noexcept
    -> SemanticRole {
  switch (type) {
  case ElementType::Button:
    return SemanticRole::Button;
  case ElementType::Text:
    return SemanticRole::Text;
  case ElementType::TextField:
  case ElementType::TextArea:
    return SemanticRole::TextBox;
  case ElementType::Image:
    return SemanticRole::Image;
  case ElementType::ListView:
    return SemanticRole::List;
  case ElementType::ListItem:
    return SemanticRole::ListItem;
  case ElementType::Tab:
    return SemanticRole::Tab;
  case ElementType::MenuItem:
    return SemanticRole::MenuItem;
  case ElementType::Popup:
    return SemanticRole::Dialog;
  default:
    return SemanticRole::None;
  }
}

[[nodiscard]] auto DefaultActions(const SemanticRole role) noexcept
    -> SemanticActionFlags {
  switch (role) {
  case SemanticRole::Button:
  case SemanticRole::Link:
  case SemanticRole::MenuItem:
    return SemanticActionFlags::Activate | SemanticActionFlags::Focus;
  case SemanticRole::ListItem:
  case SemanticRole::Tab:
    return SemanticActionFlags::Activate | SemanticActionFlags::Focus |
           SemanticActionFlags::Select |
           SemanticActionFlags::ScrollIntoView;
  case SemanticRole::ComboBox:
    return SemanticActionFlags::Activate | SemanticActionFlags::Focus |
           SemanticActionFlags::Expand | SemanticActionFlags::Collapse;
  case SemanticRole::TextBox:
    return SemanticActionFlags::Focus | SemanticActionFlags::SetValue;
  default:
    return SemanticActionFlags::None;
  }
}

} // namespace

void SemanticTree::AppendRuntimeNode(const RuntimeTree &runtimeTree,
                                     const ElementHandle runtimeHandle,
                                     const SemanticNodeId semanticParent) {
  const auto *runtimeNode = runtimeTree.Get(runtimeHandle);
  if (runtimeNode == nullptr || runtimeNode->properties.semantics.hidden ||
      runtimeNode->properties.visibility != ElementVisibility::Visible) {
    return;
  }

  const auto &properties = runtimeNode->type == ElementType::CustomElement
                               ? runtimeNode->custom.semantics
                               : runtimeNode->properties.semantics;
  const auto role = properties.role == SemanticRole::None
                        ? DefaultRole(runtimeNode->type)
                        : properties.role;
  auto nextParent = semanticParent;
  if (role != SemanticRole::None) {
    auto states = properties.states;
    if (!runtimeNode->properties.interaction.enabled) {
      states |= SemanticStateFlags::Disabled;
    }
    if (runtimeNode->interaction.focused) {
      states |= SemanticStateFlags::Focused;
    }
    if (runtimeNode->interaction.pressed) {
      states |= SemanticStateFlags::Pressed;
    }
    const auto isTextEditor = runtimeNode->type == ElementType::TextField ||
                              runtimeNode->type == ElementType::TextArea;
    if (isTextEditor && runtimeNode->properties.textField.readOnly) {
      states |= SemanticStateFlags::ReadOnly;
    }
    auto semanticValue = properties.value;
    if (isTextEditor && runtimeNode->properties.textField.password) {
      semanticValue = {};
    } else if (isTextEditor && runtimeNode->textField.editing) {
      semanticValue = runtimeNode->textField.editing->Value();
    }

    const SemanticNodeId id{runtimeNode->id.value};
    m_nodes.push_back(SemanticNode{
        .id = id,
        .parent = semanticParent,
        .role = role,
        .identifier = properties.identifier,
        .labelFor = properties.labelFor,
        .labelledBy = properties.labelledBy,
        .label = properties.label,
        .value = std::move(semanticValue),
        .description = properties.description,
        .range = properties.range,
        .collectionItem = properties.collectionItem,
        .bounds = runtimeNode->type == ElementType::Popup
                      ? runtimeNode->popup.contentBounds
                      : runtimeNode->arrangedBounds,
        .states = states,
        .actions = properties.actions == SemanticActionFlags::None
                       ? DefaultActions(role)
                       : properties.actions,
        .live = properties.live,
        .password = isTextEditor && runtimeNode->properties.textField.password,
        .owner = runtimeNode->id,
    });
    const auto parent =
        std::find_if(m_nodes.begin(), m_nodes.end(),
                     [semanticParent](const SemanticNode &node) {
                       return node.id == semanticParent;
                     });
    if (parent != m_nodes.end()) {
      parent->children.push_back(id);
    }
    nextParent = id;
  }

  for (const auto child : runtimeNode->children) {
    AppendRuntimeNode(runtimeTree, child, nextParent);
  }
}

auto SemanticTree::Root() const noexcept -> SemanticNodeId { return m_root; }

auto SemanticTree::Nodes() const noexcept -> const std::vector<SemanticNode> & {
  return m_nodes;
}

auto SemanticTree::Find(const SemanticNodeId id) const noexcept
    -> const SemanticNode * {
  const auto found =
      std::find_if(m_nodes.begin(), m_nodes.end(),
                   [id](const SemanticNode &node) { return node.id == id; });
  return found == m_nodes.end() ? nullptr : &*found;
}

auto SemanticTree::FindByOwner(const ElementId owner) const noexcept
    -> const SemanticNode * {
  const auto found = std::find_if(
      m_nodes.begin(), m_nodes.end(),
      [owner](const SemanticNode &node) { return node.owner == owner; });
  return found == m_nodes.end() ? nullptr : &*found;
}

auto BuildSemanticTree(const RuntimeTree &tree,
                       const NGIN::Text::String &windowLabel) -> SemanticTree {
  SemanticTree result;
  const auto *runtimeRoot = tree.Get(tree.Root());
  if (runtimeRoot == nullptr) {
    return result;
  }

  result.m_root = SemanticNodeId{runtimeRoot->id.value};
  result.m_nodes.push_back(SemanticNode{
      .id = result.m_root,
      .role = SemanticRole::Window,
      .label = windowLabel,
      .bounds = runtimeRoot->arrangedBounds,
      .owner = runtimeRoot->id,
  });
  for (const auto child : runtimeRoot->children) {
    result.AppendRuntimeNode(tree, child, result.m_root);
  }
  return result;
}
} // namespace NGIN::UI
