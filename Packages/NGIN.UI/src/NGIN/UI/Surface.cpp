#include "MotionInternal.hpp"
#include "RetainedDisplayList.hpp"
#include "SurfaceComposition.hpp"
#include <NGIN/UI/Surface.hpp>
#include "ComponentPrimitives.hpp"

#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>

namespace NGIN::UI {
namespace {
using Clock = std::chrono::steady_clock;
auto MillisecondsSince(Clock::time_point start) -> F64 {
  return std::chrono::duration<F64, std::milli>{Clock::now() - start}.count();
}

auto ChangesFrom(const InputDispatchResult &result) noexcept
    -> InvalidationKind {
  if (result.invalidation != InvalidationKind::None)
    return result.invalidation;
  if (result.activated || result.callbackInvoked)
    return InvalidationKind::All;
  if (result.layoutStateChanged)
    return InvalidationKind::Arrange;
  if (result.visualStateChanged)
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  return InvalidationKind::None;
}

[[nodiscard]] auto ActionFlag(const SemanticActionKind action) noexcept
    -> SemanticActionFlags {
  switch (action) {
  case SemanticActionKind::Activate:
    return SemanticActionFlags::Activate;
  case SemanticActionKind::Focus:
    return SemanticActionFlags::Focus;
  case SemanticActionKind::SetValue:
    return SemanticActionFlags::SetValue;
  case SemanticActionKind::Increment:
    return SemanticActionFlags::Increment;
  case SemanticActionKind::Decrement:
    return SemanticActionFlags::Decrement;
  case SemanticActionKind::Select:
    return SemanticActionFlags::Select;
  case SemanticActionKind::Expand:
    return SemanticActionFlags::Expand;
  case SemanticActionKind::Collapse:
    return SemanticActionFlags::Collapse;
  case SemanticActionKind::ScrollIntoView:
    return SemanticActionFlags::ScrollIntoView;
  case SemanticActionKind::Realize:
    return SemanticActionFlags::Realize;
  }
  return SemanticActionFlags::None;
}
} // namespace

struct Surface::Implementation final {
  explicit Implementation(SurfaceOptions settings)
      : options(std::move(settings)),
        inputRouter(tree, options.platform, options.window),
        semanticTree(BuildSemanticTree(tree, options.title)),
        motionHost(Detail::CreateMotionHost([this] {
          Invalidate(InvalidationKind::Paint | InvalidationKind::Semantics);
        })) {}

  void Invalidate(InvalidationKind kind, bool targeted = false) noexcept {
    if (closed || kind == InvalidationKind::None)
      return;
    const bool wake = pending == InvalidationKind::None;
    pending |= kind;
    if (!targeted && kind != InvalidationKind::Compose &&
        kind != InvalidationKind::Semantics)
      forcePaint = true;
    diagnostics.lastInvalidation = kind;
    if (wake && options.platform)
      options.platform->WakeEventLoop();
  }

  void ForgetPaint(ElementHandle handle) noexcept {
    while (const auto *node = tree.Get(handle)) {
      recorded.slices.erase(node->id.value);
      handle = node->parent;
    }
  }

  // Invalidate each cached ancestor of input targets that can change visual
  // state. Other immutable components can reuse their previous display slices.
  void ForgetInputTargets(const PlatformEvent &event) noexcept {
    if (recorded.slices.empty())
      return;
    ForgetPaint(inputRouter.FocusedElement());
    std::visit(
        [this](const auto &value) {
          if constexpr (requires {
                          value.pointerId;
                          value.position;
                        }) {
            ForgetPaint(inputRouter.HoveredElement(value.pointerId));
            ForgetPaint(inputRouter.CapturedElement(value.pointerId));
            ForgetPaint(inputRouter.HitTest(value.position));
          }
        },
        event);
  }

  void InvalidateInput(const InputDispatchResult &result,
                       bool targeted) noexcept {
    auto kind = ChangesFrom(result);
    // Legacy handlers can write arbitrary state. An observed component write
    // already requested composition, which refreshes changed declarations.
    if (kind == InvalidationKind::All &&
        HasInvalidation(pending, InvalidationKind::Compose)) {
      kind = InvalidationKind::Compose | InvalidationKind::Arrange |
             InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    Invalidate(kind, targeted && kind != InvalidationKind::All);
  }

  [[nodiscard]] auto ReducedMotion() const noexcept -> bool {
    return !motionEnabled ||
           (options.platform && options.platform->ReducedMotionEnabled());
  }

  // Input, animation and paint callbacks may replace their own screen. Retire
  // its nodes only after the outermost callback has returned.
  void FlushContent() noexcept {
    if (callbackDepth || !replacement)
      return;
    ++callbackDepth;
    do {
      auto next = std::move(*replacement);
      replacement.reset();
      static_cast<void>(inputRouter.Deactivate());
      semanticTree = {};
      ++semanticRevision;
      diagnostics.reconciliation = {};
      diagnostics.layout = {};
      diagnostics.semanticNodeCount = 0;
      diagnostics.displayCommandCount = 0;
      diagnostics.drawBatchCount = 0;
      diagnostics.vertexCount = 0;
      diagnostics.indexCount = 0;
      diagnostics.activeAnimationCount = 0;
      diagnostics.motion = {};
      diagnostics.focusedElement = {};
      diagnostics.pointerCaptureOwner = {};
      diagnostics.reusedPaintSubtrees = 0;
      tree.Clear();
      content = std::move(next);
      recorded = {};
      packet.reset();
      nextDeadline.reset();
    } while (replacement);
    --callbackDepth;
  }

  struct CallbackScope {
    explicit CallbackScope(std::shared_ptr<Implementation> owner)
        : state(std::move(owner)) {
      ++state->callbackDepth;
    }
    ~CallbackScope() {
      --state->callbackDepth;
      state->FlushContent();
    }
    std::shared_ptr<Implementation> state;
  };

  SurfaceOptions options;
  using Content = Detail::SurfaceComposition::Content;
  Content content{};
  std::optional<Content> replacement{};
  UIntSize callbackDepth{};
  RuntimeTree tree{};
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter inputRouter;
  UIRenderer renderer{};
  Frame packet{};
  Detail::RetainedDisplayList recorded{};
  PixelSize extent{};
  F32 scale{1.0F};
  SemanticTree semanticTree;
  UInt64 semanticRevision{1};
  WindowDiagnostics diagnostics{};
  InspectorOverlayOptions inspectorOverlay{};
  InvalidationKind pending{InvalidationKind::All};
  bool closed{};
  bool forcePaint{true};
  bool motionEnabled{true};
  std::optional<MonotonicTime> nextDeadline{};
  std::shared_ptr<Detail::MotionHostState> motionHost;
};

Surface::Surface(SurfaceOptions options)
    : m_implementation(std::make_shared<Implementation>(std::move(options))) {}
Surface::~Surface() { Close(); }

void Surface::Close() noexcept {
  auto &state = *m_implementation;
  if (state.closed)
    return;
  state.closed = true;
  Detail::CloseMotionHost(state.motionHost);
  state.replacement.emplace();
  state.FlushContent();
  state.pending = InvalidationKind::None;
}

void Surface::SetContent(Views::View view, Views::ViewEnvironment environment) {
  Detail::SurfaceComposition::SetContent(
      *this, Views::Mount(std::move(view), std::move(environment), Invalidator()));
}

void Detail::SurfaceComposition::SetContent(Surface &surface, Content content) {
  const auto state = surface.m_implementation;
  if (state->closed)
    return;
  state->replacement.emplace(std::move(content));
  state->FlushContent();
  state->Invalidate(InvalidationKind::All);
}

void Surface::Invalidate(InvalidationKind kind) noexcept {
  m_implementation->Invalidate(kind);
}
auto Surface::Invalidator() const -> InvalidationScheduler {
  return [owner = std::weak_ptr{m_implementation}](InvalidationKind kind) {
    if (auto state = owner.lock())
      state->Invalidate(kind);
  };
}
auto Surface::Tree() const noexcept -> const RuntimeTree & {
  return m_implementation->tree;
}
auto Surface::Semantics() const noexcept -> const SemanticTree & {
  return m_implementation->semanticTree;
}
auto Surface::SemanticRevision() const noexcept -> UInt64 {
  return m_implementation->semanticRevision;
}
auto Surface::Diagnostics() const noexcept -> const WindowDiagnostics & {
  return m_implementation->diagnostics;
}
auto Surface::HitTest(Point position) const noexcept -> ElementHandle {
  return m_implementation->inputRouter.HitTest(position);
}
auto Surface::FocusedElement() const noexcept -> ElementHandle {
  return m_implementation->inputRouter.FocusedElement();
}
auto Surface::CapturedElement(UInt64 pointerId) const noexcept
    -> ElementHandle {
  return m_implementation->inputRouter.CapturedElement(pointerId);
}
auto Surface::NextAnimationDeadline() const noexcept
    -> std::optional<MonotonicTime> {
  return m_implementation->nextDeadline;
}
auto Surface::MotionEnabled() const noexcept -> bool {
  return m_implementation->motionEnabled;
}
void Surface::SetMotionEnabled(bool enabled) noexcept {
  if (enabled == m_implementation->motionEnabled)
    return;
  m_implementation->motionEnabled = enabled;
  Invalidate(InvalidationKind::Paint | InvalidationKind::Semantics);
}
auto Surface::InspectorOverlay() const noexcept
    -> const InspectorOverlayOptions & {
  return m_implementation->inspectorOverlay;
}

auto Surface::Focus(const ElementHandle handle) noexcept -> bool {
  if (!m_implementation->inputRouter.SetFocus(handle)) {
    return false;
  }
  Invalidate(InvalidationKind::Paint | InvalidationKind::Semantics);
  return true;
}

auto Surface::FocusNext(const bool reverse) -> bool {
  if (!m_implementation->inputRouter.MoveFocus(reverse)) {
    return false;
  }
  Invalidate(InvalidationKind::Paint | InvalidationKind::Semantics);
  return true;
}

auto Surface::PerformSemanticAction(
    const SemanticActionRequest &request) noexcept -> UIResult<void> {
  if (m_implementation->closed || m_implementation->callbackDepth) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidState, "Surface is closed or dispatching",
        "NGIN.UI", "Surface::PerformSemanticAction",
        m_implementation->options.title.c_str()));
  }
  const auto *semantic = m_implementation->semanticTree.Find(request.node);
  if (semantic == nullptr) {
    return std::unexpected(
        MakeUIError(UIErrorCode::InvalidArgument,
                    "The semantic element is no longer available", "NGIN.UI",
                    "Surface::PerformSemanticAction",
                    m_implementation->options.title.c_str()));
  }
  if (!HasSemanticAction(semantic->actions, ActionFlag(request.action))) {
    return std::unexpected(
        MakeUIError(UIErrorCode::Unsupported,
                    "The semantic element does not support this action",
                    "NGIN.UI", "Surface::PerformSemanticAction",
                    m_implementation->options.title.c_str()));
  }
  const auto target = m_implementation->tree.FindById(semantic->owner);
  if (!target) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidState, "The semantic element owner was destroyed",
        "NGIN.UI", "Surface::PerformSemanticAction",
        m_implementation->options.title.c_str()));
  }
  try {
    Implementation::CallbackScope scope{m_implementation};
    scope.state->ForgetPaint(target);
    scope.state->ForgetPaint(scope.state->inputRouter.FocusedElement());
    auto dispatched =
        m_implementation->inputRouter.PerformSemanticAction(target, request);
    if (!dispatched) {
      return std::unexpected(std::move(dispatched).error());
    }
    const auto &result = dispatched.value();
    scope.state->ForgetPaint(scope.state->inputRouter.FocusedElement());
    scope.state->InvalidateInput(result, true);
    return {};
  } catch (...) {
    return std::unexpected(
        MakeUIError(UIErrorCode::InvalidState,
                    "The semantic action callback threw an exception",
                    "NGIN.UI", "Surface::PerformSemanticAction",
                    m_implementation->options.title.c_str()));
  }
}

void Surface::SetInspectorOverlay(InspectorOverlayOptions options) {
  m_implementation->inspectorOverlay = std::move(options);
  Invalidate(InvalidationKind::Paint);
}

auto Surface::Route(const PlatformEvent &event) -> InputDispatchResult {
  if (m_implementation->closed || m_implementation->callbackDepth)
    return {};
  Implementation::CallbackScope scope{m_implementation};
  scope.state->ForgetInputTargets(event);
  auto result = scope.state->inputRouter.Route(event);
  scope.state->ForgetInputTargets(event);
  scope.state->InvalidateInput(
      result, !std::holds_alternative<WindowFocusChanged>(event));
  return result;
}
auto Surface::IsDirty() const noexcept -> bool {
  return m_implementation->pending != InvalidationKind::None;
}
auto Surface::NeedsUpdate(MonotonicTime now) const noexcept -> bool {
  const auto &state = *m_implementation;
  return !state.closed &&
         (IsDirty() || (state.nextDeadline && *state.nextDeadline <= now) ||
          state.ReducedMotion() != state.diagnostics.reducedMotion);
}
void Surface::RecordPresentation(
    const FrameTimingDiagnostics &timings) noexcept {
  m_implementation->diagnostics.frameTimings = timings;
}
auto Surface::Update(PixelSize extent, F32 scaleFactor, MonotonicTime now)
    -> UIResult<Frame> {
  auto &state = *m_implementation;
  if (state.closed || state.callbackDepth > 0) {
    return std::unexpected(MakeUIError(UIErrorCode::InvalidState,
                                       "Surface is closed or already updating",
                                       "NGIN.UI", "Surface::Update"));
  }
  if (extent.IsEmpty() || !std::isfinite(scaleFactor) || scaleFactor <= 0) {
    return std::unexpected(
        MakeUIError(UIErrorCode::InvalidArgument,
                    "Expected nonempty pixel extent and positive finite scale",
                    "NGIN.UI", "Surface::Update"));
  }
  if (state.extent != extent || state.scale != scaleFactor) {
    state.pending |= InvalidationKind::Measure;
    state.forcePaint = true;
    state.extent = extent;
    state.scale = scaleFactor;
    state.inputRouter.SetScaleFactor(scaleFactor);
  }
  if (!NeedsUpdate(now))
    return state.packet;

  const auto start = Clock::now();
  auto stages = std::exchange(state.pending, InvalidationKind::None);
  const bool incremental = !std::exchange(state.forcePaint, false);
  const bool reuseLayout = stages != InvalidationKind::All;
  Implementation::CallbackScope scope{m_implementation};
  struct EndUpdate {
    Implementation &state;
    bool success{};
    ~EndUpdate() {
      if (!success && !state.closed)
        state.pending |= InvalidationKind::All;
    }
  } guard{state};
  auto &diagnostics = state.diagnostics;
  FrameTimingDiagnostics timings{};
  try {
    if (HasInvalidation(stages, InvalidationKind::Compose)) {
      const auto phase = Clock::now();
      Composer composer;
      if (state.content)
        state.content(composer);
      if (!composer.IsBalanced()) {
        return std::unexpected(
            MakeUIError(UIErrorCode::InvalidState,
                        "Composition ended with an open element scope",
                        "NGIN.UI", "Surface::Update"));
      }
      diagnostics.reconciliation =
          state.reconciler.Reconcile(composer.Declarations());
      const auto focused = state.inputRouter.FocusedElement();
      state.inputRouter.Synchronize();
      if (focused != state.inputRouter.FocusedElement()) {
        state.ForgetPaint(focused);
        state.ForgetPaint(state.inputRouter.FocusedElement());
      }
      ++diagnostics.compositionCount;
      stages |= InvalidationKind::Measure;
      timings.compositionMilliseconds = MillisecondsSince(phase);
    }
    if (HasInvalidation(stages, InvalidationKind::Measure) ||
        HasInvalidation(stages, InvalidationKind::Arrange)) {
      const auto phase = Clock::now();
      // Callers holding an old frame continue to pin the atlas pages it uses.
      state.packet.reset();
      const Size logical{static_cast<F32>(extent.width) / scaleFactor,
                         static_cast<F32>(extent.height) / scaleFactor};
      diagnostics.layout = state.layout.Perform(
          {.minimum = logical, .maximum = logical},
          {0, 0, logical.width, logical.height}, scaleFactor, reuseLayout);
      stages |= InvalidationKind::Paint | InvalidationKind::Semantics;
      ++diagnostics.layoutCount;
      timings.layoutMilliseconds = MillisecondsSince(phase);
    }

    const bool reducedMotion = state.ReducedMotion();
    const auto motion =
        Detail::AdvanceMotion(state.tree, now, reducedMotion, state.motionHost);
    state.nextDeadline = motion.nextDeadline;
    diagnostics.activeAnimationCount = motion.activeElementCount;
    diagnostics.reducedMotion = reducedMotion;
    Detail::CollectMotionDiagnostics(state.tree, diagnostics.motion);
    if (motion.changed) {
      ++diagnostics.motionFrameCount;
      stages |= InvalidationKind::Paint | InvalidationKind::Semantics;
    }

    if (HasInvalidation(stages, InvalidationKind::Paint)) {
      const auto phase = Clock::now();
      auto recorded = Detail::RecordDisplayList(
          state.tree,
          incremental && !motion.changed &&
                  (HasInvalidation(stages, InvalidationKind::Measure) ||
                   HasInvalidation(stages, InvalidationKind::Arrange))
              ? &state.recorded
              : nullptr);
      auto &commands = recorded.commands;
      if (state.inspectorOverlay.enabled) {
        auto overlay =
            BuildInspectorOverlay(state.tree, state.inspectorOverlay);
        commands.insert(commands.end(),
                        std::make_move_iterator(overlay.begin()),
                        std::make_move_iterator(overlay.end()));
      }
      auto packet = std::make_shared<PreparedRenderPacket>(
          state.renderer.Build(commands, extent, scaleFactor));
      packet->loadAction = state.options.loadAction;
      diagnostics.displayCommandCount = commands.size();
      diagnostics.drawBatchCount = packet->batches.size();
      diagnostics.vertexCount = packet->vertices.size();
      diagnostics.indexCount = packet->indices.size();
      diagnostics.reusedPaintSubtrees = recorded.reusedSubtrees;
      if (!recorded.slices.empty())
        recorded.ReleaseGlyphLeases();
      state.recorded = recorded.slices.empty() ? Detail::RetainedDisplayList{}
                                               : std::move(recorded);
      state.packet = std::move(packet);
      ++diagnostics.paintCount;
      timings.paintMilliseconds = MillisecondsSince(phase);
    }
    if (HasInvalidation(stages, InvalidationKind::Semantics)) {
      const auto phase = Clock::now();
      state.semanticTree = BuildSemanticTree(state.tree, state.options.title);
      ++state.semanticRevision;
      diagnostics.semanticNodeCount = state.semanticTree.Nodes().size();
      timings.semanticsMilliseconds = MillisecondsSince(phase);
    }
    diagnostics.focusedElement = state.inputRouter.FocusedElement();
    diagnostics.pointerCaptureOwner = state.inputRouter.FirstCapturedElement();
    ++diagnostics.frameCount;
    timings.totalMilliseconds = MillisecondsSince(start);
    diagnostics.frameTimings = timings;
    guard.success = true;
    return state.packet;
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeUIError(UIErrorCode::OutOfMemory,
                                       "Surface update allocation failed",
                                       "NGIN.UI", "Surface::Update"));
  } catch (...) {
    return std::unexpected(MakeUIError(UIErrorCode::InvalidState,
                                       "Surface update callback threw",
                                       "NGIN.UI", "Surface::Update"));
  }
}
} // namespace NGIN::UI
