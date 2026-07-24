#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/Invalidation.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

namespace NGIN::UI {
struct WindowDiagnostics final {
  UInt64 frameCount{0};
  UInt64 compositionCount{0};
  ReconcileStats reconciliation{};
  LayoutPassStats layout{};
  UIntSize semanticNodeCount{0};
  UIntSize displayCommandCount{0};
  UIntSize drawBatchCount{0};
  UIntSize vertexCount{0};
  UIntSize indexCount{0};
  InvalidationKind lastInvalidation{InvalidationKind::None};
  ElementHandle focusedElement{};
  ElementHandle pointerCaptureOwner{};
};
} // namespace NGIN::UI
