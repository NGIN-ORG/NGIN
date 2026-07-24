#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>

#include <vector>

namespace NGIN::UI {
enum class SemanticRole : UInt8 {
  None,
  Window,
  Group,
  Heading,
  Text,
  Button,
  CheckBox,
  TextBox,
  List,
  ListItem,
  Image,
  Link,
  Slider,
};

enum class SemanticStateFlags : UInt16 {
  None = 0,
  Disabled = 1U << 0U,
  Focused = 1U << 1U,
  Pressed = 1U << 2U,
  Selected = 1U << 3U,
  Checked = 1U << 4U,
  Expanded = 1U << 5U,
};

enum class SemanticActionFlags : UInt8 {
  None = 0,
  Activate = 1U << 0U,
  Focus = 1U << 1U,
  SetValue = 1U << 2U,
  Increment = 1U << 3U,
  Decrement = 1U << 4U,
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
  return static_cast<SemanticActionFlags>(static_cast<UInt8>(left) |
                                          static_cast<UInt8>(right));
}

[[nodiscard]] constexpr auto
HasSemanticAction(const SemanticActionFlags value,
                  const SemanticActionFlags flag) noexcept -> bool {
  return (static_cast<UInt8>(value) & static_cast<UInt8>(flag)) != 0;
}

struct SemanticProperties final {
  SemanticRole role{SemanticRole::None};
  NGIN::Text::String label{};
  NGIN::Text::String value{};
  NGIN::Text::String description{};
  SemanticStateFlags states{SemanticStateFlags::None};
  SemanticActionFlags actions{SemanticActionFlags::None};
  bool hidden{false};
};

struct SemanticNodeId final {
  UInt64 value{0};

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
    return value != 0;
  }

  constexpr explicit operator bool() const noexcept { return IsValid(); }

  [[nodiscard]] constexpr auto
  operator<=>(const SemanticNodeId &) const noexcept = default;
};

struct SemanticNode final {
  SemanticNodeId id{};
  SemanticRole role{SemanticRole::None};
  NGIN::Text::String label{};
  NGIN::Text::String value{};
  NGIN::Text::String description{};
  Rect bounds{};
  SemanticStateFlags states{SemanticStateFlags::None};
  SemanticActionFlags actions{SemanticActionFlags::None};
  ElementId owner{};
  std::vector<SemanticNodeId> children{};
};

class RuntimeTree;

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
