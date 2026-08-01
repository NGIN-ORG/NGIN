#pragma once

#include <NGIN/UI/Motion.hpp>
#include <NGIN/UI/RuntimeTree.hpp>

#include <optional>

namespace NGIN::UI::Detail {
struct MotionSnapshot final {
  F32 value{0.0F};
  F32 opacity{1.0F};
  MotionTransform transform{};
  Color background{};
  Color foreground{};
  Color borderColor{};
  F32 focusOpacity{0.0F};
  bool hasBackground{false};
  bool hasForeground{false};
  bool hasBorderColor{false};
  bool active{false};
};

struct MotionFrameResult final {
  UIntSize activeElementCount{0};
  std::optional<MonotonicTime> nextDeadline{};
  bool changed{false};
};

[[nodiscard]] auto AdvanceMotion(RuntimeTree &tree, MonotonicTime now,
                                 bool reducedMotion) -> MotionFrameResult;
void CollectMotionDiagnostics(const RuntimeTree &tree,
                              MotionDiagnostics &diagnostics);
[[nodiscard]] auto SnapshotFor(const RuntimeNode &node) noexcept
    -> MotionSnapshot;
[[nodiscard]] auto TransformFor(const RuntimeNode &node) noexcept
    -> MotionTransform;
[[nodiscard]] auto TransformPoint(Point point,
                                  MotionTransform transform) noexcept -> Point;
[[nodiscard]] auto InverseTransformPoint(Point point,
                                         MotionTransform transform) noexcept
    -> Point;
[[nodiscard]] auto TransformRect(Rect rect,
                                 MotionTransform transform) noexcept -> Rect;
[[nodiscard]] auto ComposedTransformFor(const RuntimeTree &tree,
                                        ElementHandle handle) noexcept
    -> MotionTransform;
[[nodiscard]] auto TransformedBoundsFor(const RuntimeTree &tree,
                                        ElementHandle handle,
                                        Rect bounds) noexcept -> Rect;
} // namespace NGIN::UI::Detail
