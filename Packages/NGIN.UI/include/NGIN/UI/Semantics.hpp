#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>

#include <optional>
#include <vector>

namespace NGIN::UI {
/// @brief Accessibility role exposed for a semantic node.
enum class SemanticRole : UInt8 {
  None,
  Window,
  Group,
  Heading,
  Text,
  Button,
  CheckBox,
  RadioButton,
  Switch,
  TextBox,
  List,
  ListItem,
  Image,
  Link,
  Slider,
  ProgressBar,
  ComboBox,
  TabList,
  Tab,
  TabPanel,
  Menu,
  MenuItem,
  Dialog,
};

/// @brief Accessibility states currently held by a semantic node.
enum class SemanticStateFlags : UInt16 {
  None = 0,
  Disabled = 1U << 0U,
  Focused = 1U << 1U,
  Pressed = 1U << 2U,
  Selected = 1U << 3U,
  Checked = 1U << 4U,
  Expanded = 1U << 5U,
  Indeterminate = 1U << 6U,
  ReadOnly = 1U << 7U,
  MultiSelectable = 1U << 8U,
  Required = 1U << 9U,
  Virtualized = 1U << 10U,
};

/// @brief Accessibility actions supported by a semantic node.
enum class SemanticActionFlags : UInt16 {
  None = 0,
  Activate = 1U << 0U,
  Focus = 1U << 1U,
  SetValue = 1U << 2U,
  Increment = 1U << 3U,
  Decrement = 1U << 4U,
  Select = 1U << 5U,
  Expand = 1U << 6U,
  Collapse = 1U << 7U,
  ScrollIntoView = 1U << 8U,
  Realize = 1U << 9U,
};

[[nodiscard]] constexpr auto operator|(const SemanticStateFlags left,
                                       const SemanticStateFlags right) noexcept
    -> SemanticStateFlags {
  return static_cast<SemanticStateFlags>(static_cast<UInt16>(left) |
                                         static_cast<UInt16>(right));
}

constexpr auto operator|=(SemanticStateFlags &left,
                          const SemanticStateFlags right) noexcept
    -> SemanticStateFlags & {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr auto
HasSemanticState(const SemanticStateFlags value,
                 const SemanticStateFlags flag) noexcept -> bool {
  return (static_cast<UInt16>(value) & static_cast<UInt16>(flag)) != 0;
}

[[nodiscard]] constexpr auto operator|(const SemanticActionFlags left,
                                       const SemanticActionFlags right) noexcept
    -> SemanticActionFlags {
  return static_cast<SemanticActionFlags>(static_cast<UInt16>(left) |
                                          static_cast<UInt16>(right));
}

[[nodiscard]] constexpr auto
HasSemanticAction(const SemanticActionFlags value,
                  const SemanticActionFlags flag) noexcept -> bool {
  return (static_cast<UInt16>(value) & static_cast<UInt16>(flag)) != 0;
}

/// @brief Announcement urgency for a semantic live region.
enum class SemanticLiveSetting : UInt8 {
  Off,
  Polite,
  Assertive,
};

/// @brief Position metadata for an item inside a semantic collection.
struct SemanticCollectionItem final {
  UIntSize position{0};
  UIntSize count{0};
  UIntSize level{0};

  [[nodiscard]] constexpr auto
  operator<=>(const SemanticCollectionItem &) const noexcept = default;
};

/// @brief Numeric value, bounds, and step exposed to accessibility clients.
struct SemanticRange final {
  F64 minimum{0.0};
  F64 maximum{1.0};
  F64 current{0.0};
  F64 step{0.0};

  [[nodiscard]] constexpr auto
  operator<=>(const SemanticRange &) const noexcept = default;
};

/// @brief Authored accessibility identity, text, role, state, and actions.
struct SemanticProperties final {
  SemanticRole role{SemanticRole::None};
  NGIN::Text::String identifier{};
  NGIN::Text::String labelFor{};
  NGIN::Text::String labelledBy{};
  NGIN::Text::String label{};
  NGIN::Text::String value{};
  NGIN::Text::String description{};
  std::optional<SemanticRange> range{};
  std::optional<SemanticCollectionItem> collectionItem{};
  SemanticStateFlags states{SemanticStateFlags::None};
  SemanticActionFlags actions{SemanticActionFlags::None};
  SemanticLiveSetting live{SemanticLiveSetting::Off};
  bool hidden{false};
};

/// @brief Stable semantic identity derived from a runtime element generation.
struct SemanticNodeId final {
  UInt64 value{0};

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
    return value != 0;
  }

  constexpr explicit operator bool() const noexcept { return IsValid(); }

  [[nodiscard]] constexpr auto
  operator<=>(const SemanticNodeId &) const noexcept = default;
};

/// @brief Operation requested by an accessibility provider.
enum class SemanticActionKind : UInt8 {
  Activate,
  Focus,
  SetValue,
  Increment,
  Decrement,
  Select,
  Expand,
  Collapse,
  ScrollIntoView,
  Realize,
};

/// @brief Semantic target and optional text or numeric action argument.
struct SemanticActionRequest final {
  SemanticNodeId node{};
  SemanticActionKind action{SemanticActionKind::Activate};
  NGIN::Text::String value{};
  F64 numericValue{0.0};
};

/// @brief Resolved accessibility node with hierarchy and logical bounds.
struct SemanticNode final {
  SemanticNodeId id{};
  SemanticNodeId parent{};
  SemanticRole role{SemanticRole::None};
  NGIN::Text::String identifier{};
  NGIN::Text::String labelFor{};
  NGIN::Text::String labelledBy{};
  NGIN::Text::String label{};
  NGIN::Text::String value{};
  NGIN::Text::String description{};
  std::optional<SemanticRange> range{};
  std::optional<SemanticCollectionItem> collectionItem{};
  Rect bounds{};
  SemanticStateFlags states{SemanticStateFlags::None};
  SemanticActionFlags actions{SemanticActionFlags::None};
  SemanticLiveSetting live{SemanticLiveSetting::Off};
  bool password{false};
  ElementId owner{};
  std::vector<SemanticNodeId> children{};
};

/// @brief Owns stable runtime elements from which semantics are derived.
class RuntimeTree;

/// @brief Builds and queries the accessibility projection of a runtime tree.
class SemanticTree final {
public:
  [[nodiscard]] auto Root() const noexcept -> SemanticNodeId;
  [[nodiscard]] auto Nodes() const noexcept
      -> const std::vector<SemanticNode> &;
  [[nodiscard]] auto Find(SemanticNodeId id) const noexcept
      -> const SemanticNode *;
  [[nodiscard]] auto FindByOwner(ElementId owner) const noexcept
      -> const SemanticNode *;

private:
  friend auto BuildSemanticTree(const RuntimeTree &tree,
                                const NGIN::Text::String &windowLabel)
      -> SemanticTree;

  void AppendRuntimeNode(const RuntimeTree &runtimeTree,
                         ElementHandle runtimeHandle,
                         SemanticNodeId semanticParent);

  SemanticNodeId m_root{};
  std::vector<SemanticNode> m_nodes{};
};

[[nodiscard]] auto BuildSemanticTree(const RuntimeTree &tree,
                                     const NGIN::Text::String &windowLabel = {})
    -> SemanticTree;
} // namespace NGIN::UI
