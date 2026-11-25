#pragma once

#include <NGIN/UI/Diagnostics.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Inspector.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <memory>
#include <optional>

namespace NGIN::UI::Views {
/// @brief Description of a mountable component tree.
class View;
/// @brief Services and theme supplied when a component tree is mounted.
struct ViewEnvironment;
} // namespace NGIN::UI::Views

namespace NGIN::UI {
namespace Detail {
/// @brief Internal access to the surface's retained composition callback.
struct SurfaceComposition;
} // namespace Detail

/// @brief Resources borrowed by a retained surface. Platform services are
/// optional and must stay alive through the lifetime of the surface.
struct SurfaceOptions final {
  IPlatformBackend *platform{};
  PlatformWindowHandle window{};
  NGIN::Text::String title{};
  RenderLoadAction loadAction{RenderLoadAction::Preserve};
};

/// @brief A retained UI tree driven by the host's UI thread. An update produces
/// immutable draw geometry; the host owns presentation and the lifetime of GPU
/// resources identified by texture handles in the packet.
class Surface final {
public:
  using Frame = std::shared_ptr<const PreparedRenderPacket>;

  explicit Surface(SurfaceOptions options = {});
  ~Surface();
  Surface(const Surface &) = delete;
  auto operator=(const Surface &) -> Surface & = delete;

  /// @brief Replaces the mounted view and releases its component lifetime.
  void SetContent(Views::View view, Views::ViewEnvironment environment);
  void Invalidate(InvalidationKind kind = InvalidationKind::All) noexcept;
  /// @brief UI-thread callback safe to retain after surface destruction.
  [[nodiscard]] auto Invalidator() const -> InvalidationScheduler;
  [[nodiscard]] auto IsDirty() const noexcept -> bool;
  [[nodiscard]] auto NextAnimationDeadline() const noexcept
      -> std::optional<MonotonicTime>;

  /// @brief Updates dirty stages for the host's pixel extent, device scale,
  /// and monotonic animation time. Idle updates return the same frame.
  [[nodiscard]] auto Update(PixelSize extent, F32 scaleFactor,
                            MonotonicTime now = {}) -> UIResult<Frame>;
  /// @brief Dispatches an input event in surface-local logical coordinates.
  /// Send WindowFocusChanged{..., false} when the host takes input back.
  [[nodiscard]] auto Route(const PlatformEvent &event) -> InputDispatchResult;
  [[nodiscard]] auto Tree() const noexcept -> const RuntimeTree &;
  [[nodiscard]] auto Semantics() const noexcept -> const SemanticTree &;
  [[nodiscard]] auto SemanticRevision() const noexcept -> UInt64;
  [[nodiscard]] auto Diagnostics() const noexcept -> const WindowDiagnostics &;
  [[nodiscard]] auto HitTest(Point position) const noexcept -> ElementHandle;
  [[nodiscard]] auto FocusedElement() const noexcept -> ElementHandle;
  [[nodiscard]] auto CapturedElement(UInt64 pointerId) const noexcept
      -> ElementHandle;
  auto Focus(ElementHandle handle) noexcept -> bool;
  auto FocusNext(bool reverse = false) -> bool;
  auto PerformSemanticAction(const SemanticActionRequest &request) noexcept
      -> UIResult<void>;
  void SetMotionEnabled(bool enabled) noexcept;
  [[nodiscard]] auto MotionEnabled() const noexcept -> bool;
  void SetInspectorOverlay(InspectorOverlayOptions options);
  [[nodiscard]] auto InspectorOverlay() const noexcept
      -> const InspectorOverlayOptions &;

private:
  friend class Application;
  friend struct Detail::SurfaceComposition;
  void Close() noexcept;
  void RecordPresentation(const FrameTimingDiagnostics &timings) noexcept;
  [[nodiscard]] auto NeedsUpdate(MonotonicTime now) const noexcept -> bool;
  struct Implementation;
  std::shared_ptr<Implementation> m_implementation;
};
} // namespace NGIN::UI
