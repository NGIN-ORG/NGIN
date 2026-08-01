#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Element.hpp>
#include <NGIN/UI/Handles.hpp>

#include <span>
#include <vector>

namespace NGIN::UI {
namespace Detail {
/// @brief Opaque retained target-value state owned by a runtime node.
struct MotionState;
}
/// @brief Mutable hover, press, focus, and pointer-capture state for a node.
struct InteractionState final {
  bool hovered{false};
  bool pressed{false};
  bool keyboardPressed{false};
  bool focused{false};
};

/// @brief Runtime offsets, extents, and viewport measurements for scrolling.
struct ScrollState final {
  Point offset{};
  Size contentSize{};
  Size viewportSize{};
  UInt64 dragPointerId{0};
  Point dragOrigin{};
  Point dragOffset{};
  bool draggingHorizontal{false};
  bool draggingVertical{false};
};

/// @brief Runtime placement and open state associated with a popup node.
struct PopupState final {
  Rect contentBounds{};
};

/// @brief Intrinsic and resolved tracks retained for one Grid node.
struct GridLayoutState final {
  std::vector<F32> columnIntrinsic{};
  std::vector<F32> rowIntrinsic{};
  std::vector<F32> resolvedColumns{};
  std::vector<F32> resolvedRows{};
};

/// @brief One retained WrapPanel line and its measured extents.
struct WrapLineLayoutState final {
  std::vector<ElementHandle> children{};
  F32 mainExtent{0.0F};
  F32 crossExtent{0.0F};
};

/// @brief Lines retained for the latest WrapPanel layout.
struct WrapPanelLayoutState final {
  std::vector<WrapLineLayoutState> lines{};
};

/// @brief Editing buffer and caret state retained for a text field.
struct TextFieldRuntimeState final {
  std::shared_ptr<TextEditingBuffer> editing{};
  IGraphemeSegmenter *graphemeSegmenter{nullptr};
};

/// @brief Glyph atlas and positioned quads retained for one text run.
struct TextGlyphRun final {
  TextureHandle texture{};
  std::vector<GlyphQuad> glyphs{};
};

/// @brief Paragraph layout and glyph geometry retained for a text node.
struct TextRuntimeState final {
  ParagraphLayout paragraph{};
  std::vector<TextGlyphRun> glyphRuns{};
  std::vector<Rect> selectionRects{};
  std::vector<Rect> compositionRects{};
  Rect caretRect{};
  bool hasCaret{false};
  bool valid{false};
};

/// @brief Resolved texture and draw rectangle retained for an image node.
struct ImageRuntimeState final {
  TextureHandle texture{};
  PixelSize sourceSize{};
  Rect destination{};
  ImageLoadState loadState{ImageLoadState::Loading};
  bool valid{false};
};

/// @brief State store and implementation instance retained for a custom node.
struct CustomElementRuntimeState final {
  std::shared_ptr<CustomStateStore> state{};
  SemanticProperties semantics{};
  F32 scaleFactor{1.0F};
};

/// @brief Reconciled element with identity, hierarchy, layout, and runtime
/// state.
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
  PopupState popup{};
  GridLayoutState grid{};
  WrapPanelLayoutState wrapPanel{};
  TextFieldRuntimeState textField{};
  TextRuntimeState text{};
  ImageRuntimeState image{};
  CustomElementRuntimeState custom{};
  std::shared_ptr<Detail::MotionState> motion{};
  UInt64 compositionRevision{0};
  UInt64 layoutRevision{0};

  [[nodiscard]] auto IsKeyed() const noexcept -> bool { return !key.Empty(); }
};

/// @brief Reuse, creation, removal, and move counts for a reconciliation pass.
struct ReconcileStats final {
  UIntSize created{0};
  UIntSize preserved{0};
  UIntSize removed{0};

  [[nodiscard]] constexpr auto
  operator<=>(const ReconcileStats &) const noexcept = default;
};

/// @brief Owns stable slots and generations for a window's reconciled elements.
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
  [[nodiscard]] auto FindById(ElementId id) const noexcept -> ElementHandle;
  [[nodiscard]] auto ResourcesFor(ElementHandle handle) const noexcept
      -> std::shared_ptr<const ResourceScope>;
  [[nodiscard]] auto
  FindBySemanticIdentifier(const NGIN::Text::String &identifier) const noexcept
      -> ElementHandle;
  [[nodiscard]] auto LiveCount() const noexcept -> UIntSize;

private:
  friend class Reconciler;
  friend class InputRouter;
  friend class LayoutEngine;

  struct Slot final {
    RuntimeNode node{};
    UInt32 generation{1};
    bool occupied{false};
  };

  [[nodiscard]] auto CreateNode(ElementType type, const NGIN::Text::String &key,
                                const NodeProperties &properties,
                                ElementHandle parent) -> ElementHandle;
  void SynchronizeTextField(RuntimeNode &node);
  void SynchronizeCustom(RuntimeNode &node, F32 scaleFactor = 1.0F);
  void UnmountCustom(RuntimeNode &node) noexcept;
  auto DestroySubtree(ElementHandle handle) noexcept -> UIntSize;

  std::vector<Slot> m_slots{};
  std::vector<UInt32> m_freeSlots{};
  ElementHandle m_root{};
  UInt64 m_nextElementId{1};
  UIntSize m_liveCount{0};
};

/// @brief Matches keyed declarations to runtime nodes while preserving state.
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
