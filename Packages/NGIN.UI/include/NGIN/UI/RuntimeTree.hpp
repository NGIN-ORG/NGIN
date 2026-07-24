#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Element.hpp>
#include <NGIN/UI/Handles.hpp>

#include <span>
#include <vector>

namespace NGIN::UI {
struct InteractionState final {
  bool hovered{false};
  bool pressed{false};
  bool keyboardPressed{false};
  bool focused{false};
};

struct ScrollState final {
  Point offset{};
  Size contentSize{};
  Size viewportSize{};
};

struct RuntimeNode final {
  ElementHandle handle{};
  ElementId id{};
  ElementHandle parent{};
  ElementType type{ElementType::Custom};
  NGIN::Text::String key{};
  NodeProperties properties{};
  std::vector<ElementHandle> children{};
  Size measuredSize{};
  Rect arrangedBounds{};
  InteractionState interaction{};
  ScrollState scroll{};
  UInt64 compositionRevision{0};
  UInt64 layoutRevision{0};

  [[nodiscard]] auto IsKeyed() const noexcept -> bool { return !key.Empty(); }
};

struct ReconcileStats final {
  UIntSize created{0};
  UIntSize preserved{0};
  UIntSize removed{0};

  [[nodiscard]] constexpr auto
  operator<=>(const ReconcileStats &) const noexcept = default;
};

class RuntimeTree final {
public:
  RuntimeTree();

  RuntimeTree(const RuntimeTree &) = delete;
  RuntimeTree(RuntimeTree &&) noexcept = default;
  auto operator=(const RuntimeTree &) -> RuntimeTree & = delete;
  auto operator=(RuntimeTree &&) noexcept -> RuntimeTree & = default;
  ~RuntimeTree() = default;

  [[nodiscard]] auto Root() const noexcept -> ElementHandle;
  [[nodiscard]] auto IsAlive(ElementHandle handle) const noexcept -> bool;
  [[nodiscard]] auto Get(ElementHandle handle) noexcept -> RuntimeNode *;
  [[nodiscard]] auto Get(ElementHandle handle) const noexcept
      -> const RuntimeNode *;
  [[nodiscard]] auto ResourcesFor(ElementHandle handle) const noexcept
      -> std::shared_ptr<const ResourceScope>;
  [[nodiscard]] auto LiveCount() const noexcept -> UIntSize;

private:
  friend class Reconciler;

  struct Slot final {
    RuntimeNode node{};
    UInt32 generation{1};
    bool occupied{false};
  };

  [[nodiscard]] auto CreateNode(ElementType type, const NGIN::Text::String &key,
                                const NodeProperties &properties,
                                ElementHandle parent) -> ElementHandle;
  auto DestroySubtree(ElementHandle handle) noexcept -> UIntSize;

  std::vector<Slot> m_slots{};
  std::vector<UInt32> m_freeSlots{};
  ElementHandle m_root{};
  UInt64 m_nextElementId{1};
  UIntSize m_liveCount{0};
};

class Reconciler final {
public:
  explicit Reconciler(RuntimeTree &tree) noexcept;

  auto Reconcile(std::span<const ElementDeclaration> declarations)
      -> ReconcileStats;

private:
  auto ReconcileChildren(ElementHandle parent,
                         std::span<const ElementDeclaration> declarations,
                         ReconcileStats &stats) -> void;
  auto MaterializeSubtree(ElementHandle parent,
                          const ElementDeclaration &declaration,
                          ReconcileStats &stats) -> ElementHandle;

  RuntimeTree &m_tree;
  UInt64 m_revision{0};
};
} // namespace NGIN::UI
