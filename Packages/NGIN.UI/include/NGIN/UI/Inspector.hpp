#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Diagnostics.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/RuntimeTree.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/Style.hpp>

#include <vector>

namespace NGIN::UI {
class Window;

struct InspectorNodeSnapshot final {
  ElementHandle handle{};
  ElementHandle parent{};
  ElementId id{};
  ElementType type{ElementType::Custom};
  NGIN::Text::String key{};
  UIntSize depth{0};
  Size measuredSize{};
  Rect arrangedBounds{};
  InteractionState interaction{};
  ScrollState scroll{};
  PopupState popup{};
  bool enabled{true};
  bool hitTestVisible{true};
  bool focusable{false};
  UInt64 compositionRevision{0};
  UInt64 layoutRevision{0};
};

struct InspectorSnapshot final {
  NGIN::Text::String windowId{};
  PixelSize pixelExtent{};
  F32 scaleFactor{1.0F};
  WindowDiagnostics diagnostics{};
  std::vector<InspectorNodeSnapshot> nodes{};
  std::vector<SemanticNode> semanticNodes{};
};

struct InspectorOverlayOptions final {
  bool enabled{false};
  bool showLayoutBounds{true};
  bool showHitTestBounds{false};
  bool showFocus{true};
  F32 strokeThickness{1.0F};
  Color layoutColor{0.1F, 0.65F, 1.0F, 0.9F};
  Color hitTestColor{0.2F, 1.0F, 0.35F, 0.9F};
  Color focusColor{1.0F, 0.25F, 0.2F, 1.0F};
  Color selectedColor{1.0F, 0.8F, 0.1F, 1.0F};
  ElementHandle selected{};
};

[[nodiscard]] auto CaptureInspectorSnapshot(const Window &window)
    -> InspectorSnapshot;
[[nodiscard]] auto BuildInspectorOverlay(const RuntimeTree &tree,
                                         const InspectorOverlayOptions &options)
    -> DisplayList;
} // namespace NGIN::UI
