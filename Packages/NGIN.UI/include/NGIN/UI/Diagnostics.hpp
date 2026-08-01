#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/Invalidation.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

namespace NGIN::UI {
/// @brief Per-frame CPU timing measurements for a window.
struct FrameTimingDiagnostics final {
  F64 compositionMilliseconds{0.0};
  F64 layoutMilliseconds{0.0};
  F64 paintMilliseconds{0.0};
  F64 semanticsMilliseconds{0.0};
  F64 renderMilliseconds{0.0};
  F64 presentMilliseconds{0.0};
  F64 totalMilliseconds{0.0};
};

/// @brief Current and peak diagnostics accumulated for a window.
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
  UIntSize activeAnimationCount{0};
  UInt64 motionFrameCount{0};
  bool reducedMotion{false};
  InvalidationKind lastInvalidation{InvalidationKind::None};
  ElementHandle focusedElement{};
  ElementHandle pointerCaptureOwner{};
  FrameTimingDiagnostics frameTimings{};
};
} // namespace NGIN::UI
