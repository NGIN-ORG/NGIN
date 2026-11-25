#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Components.hpp>

#include "UIAsyncScheduler.hpp"

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/MonotonicClock.hpp>

#include <algorithm>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace NGIN::UI {
namespace {
using DiagnosticsClock = std::chrono::steady_clock;

using Detail::UIAsyncScheduler;

[[nodiscard]] auto
ElapsedMilliseconds(const DiagnosticsClock::time_point start,
                    const DiagnosticsClock::time_point end) noexcept -> F64 {
  return std::chrono::duration<F64, std::milli>{end - start}.count();
}

[[nodiscard]] auto IsModalBlockedEvent(const PlatformEvent &event) noexcept
    -> bool {
  return std::visit(
      [](const auto &value) {
        using Event = std::remove_cvref_t<decltype(value)>;
        return !std::is_same_v<Event, WindowResized> &&
               !std::is_same_v<Event, WindowMoved> &&
               !std::is_same_v<Event, WindowScaleChanged> &&
               !std::is_same_v<Event, ThemeChanged>;
      },
      event);
}

} // namespace

struct Window::Implementation final {
  Implementation(IPlatformBackend &backend, PlatformWindowHandle handle,
                 const WindowCreateInfo &windowInfo)
      : platform(&backend),
        surface(SurfaceOptions{.platform = &backend,
                               .window = handle,
                               .title = windowInfo.title,
                               .loadAction = RenderLoadAction::Clear}) {}

  struct ScheduledEntry final {
    ScheduledActionId id{0};
    MonotonicTime due{};
    ScheduledAction action{};
  };

  IPlatformBackend *platform{nullptr};
  WindowCreateInfo info{};
  PlatformWindowHandle platformHandle{};
  RenderSurfaceHandle surfaceHandle{};
  PixelSize pixelExtent{};
  F32 scaleFactor{1.0F};
  EventHandler eventHandler{};
  Surface surface;
  bool closeRequested{false};
  bool closed{false};
  Window *owner{nullptr};
  DialogWindow *activeModalDialog{nullptr};
  ElementHandle ownerFocusToRestore{};
  ScheduledActionId nextScheduledActionId{1};
  std::vector<ScheduledEntry> scheduledActions{};
  NGIN::Async::CancellationSource lifetimeCancellation{};
};

Window::Window(WindowCreateInfo info, const PlatformWindowHandle platformHandle,
               const RenderSurfaceHandle surfaceHandle,
               IPlatformBackend &platformBackend, Window *owner)
    : m_implementation(std::make_unique<Implementation>(platformBackend,
                                                        platformHandle, info)) {
  m_implementation->pixelExtent = info.initialSize;
  m_implementation->info = std::move(info);
  m_implementation->platformHandle = platformHandle;
  m_implementation->surfaceHandle = surfaceHandle;
  m_implementation->owner = owner;
}

Window::~Window() = default;

DialogWindow::DialogWindow(WindowCreateInfo info,
                           const PlatformWindowHandle platformHandle,
                           const RenderSurfaceHandle surfaceHandle,
                           IPlatformBackend &platformBackend, Window &owner)
    : Window(std::move(info), platformHandle, surfaceHandle, platformBackend,
             &owner) {}

auto Window::Id() const noexcept -> const NGIN::Text::String & {
  return m_implementation->info.id;
}

auto Window::PlatformHandle() const noexcept -> PlatformWindowHandle {
  return m_implementation->platformHandle;
}

auto Window::SurfaceHandle() const noexcept -> RenderSurfaceHandle {
  return m_implementation->surfaceHandle;
}

auto Window::PixelExtent() const noexcept -> PixelSize {
  return m_implementation->pixelExtent;
}

auto Window::ScaleFactor() const noexcept -> F32 {
  return m_implementation->scaleFactor;
}

auto Window::IsDirty() const noexcept -> bool {
  return m_implementation->surface.IsDirty();
}

auto Window::IsClosed() const noexcept -> bool {
  return m_implementation->closed;
}

auto Window::IsCloseRequested() const noexcept -> bool {
  return m_implementation->closeRequested;
}

auto Window::Kind() const noexcept -> WindowKind {
  return m_implementation->info.kind;
}

auto Window::IsModal() const noexcept -> bool {
  return m_implementation->info.modal;
}

auto Window::Owner() const noexcept -> const Window * {
  return m_implementation->owner;
}

auto Window::ActiveModalDialog() const noexcept -> const DialogWindow * {
  const auto *dialog = m_implementation->activeModalDialog;
  return dialog != nullptr && !dialog->IsClosed() ? dialog : nullptr;
}

auto Window::Tree() const noexcept -> const RuntimeTree & {
  return m_implementation->surface.Tree();
}

auto Window::LastReconcileStats() const noexcept -> const ReconcileStats & {
  return m_implementation->surface.Diagnostics().reconciliation;
}

auto Window::LastLayoutStats() const noexcept -> const LayoutPassStats & {
  return m_implementation->surface.Diagnostics().layout;
}

auto Window::DisplayCommandCount() const noexcept -> UIntSize {
  return m_implementation->surface.Diagnostics().displayCommandCount;
}

auto Window::HasActiveAnimations() const noexcept -> bool {
  return m_implementation->surface.NextAnimationDeadline().has_value();
}

auto Window::NextAnimationDeadline() const noexcept
    -> std::optional<MonotonicTime> {
  return m_implementation->surface.NextAnimationDeadline();
}

auto Window::MotionEnabled() const noexcept -> bool {
  return m_implementation->surface.MotionEnabled();
}

auto Window::HitTest(const Point position) const noexcept -> ElementHandle {
  return m_implementation->surface.HitTest(position);
}

auto Window::FocusedElement() const noexcept -> ElementHandle {
  return m_implementation->surface.FocusedElement();
}

auto Window::CapturedElement(const UInt64 pointerId) const noexcept
    -> ElementHandle {
  return m_implementation->surface.CapturedElement(pointerId);
}

auto Window::Semantics() const noexcept -> const SemanticTree & {
  return m_implementation->surface.Semantics();
}

auto Window::Diagnostics() const noexcept -> const WindowDiagnostics & {
  return m_implementation->surface.Diagnostics();
}

auto Window::Inspect() const -> InspectorSnapshot {
  return CaptureInspectorSnapshot(*this);
}

auto Window::InspectorOverlay() const noexcept
    -> const InspectorOverlayOptions & {
  return m_implementation->surface.InspectorOverlay();
}

auto Window::Focus(const ElementHandle handle) noexcept -> bool {
  return m_implementation->surface.Focus(handle);
}

auto Window::FocusNext(const bool reverse) -> bool {
  return m_implementation->surface.FocusNext(reverse);
}

auto Window::PerformSemanticAction(
    const SemanticActionRequest &request) noexcept -> UIResult<void> {
  return m_implementation->surface.PerformSemanticAction(request);
}

void Window::SetInspectorOverlay(InspectorOverlayOptions options) {
  m_implementation->surface.SetInspectorOverlay(std::move(options));
}

void Window::SetEventHandler(EventHandler handler) {
  m_implementation->eventHandler = std::move(handler);
}

auto Window::ContentSurface() noexcept -> Surface & {
  return m_implementation->surface;
}

void Window::SetContent(Views::View view, Views::ViewEnvironment environment) {
  m_implementation->surface.SetContent(std::move(view), std::move(environment));
}

void Window::Invalidate(const InvalidationKind kind) noexcept {
  m_implementation->surface.Invalidate(kind);
}

void Window::SetMotionEnabled(const bool enabled) noexcept {
  m_implementation->surface.SetMotionEnabled(enabled);
}

auto Window::Schedule(const std::chrono::milliseconds delay,
                      ScheduledAction action) noexcept
    -> UIResult<ScheduledActionId> {
  if (m_implementation->closed) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidState, "Cannot schedule work on a closed window",
        "NGIN.UI", "Window::Schedule", Id().c_str()));
  }
  if (!action) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidArgument, "Scheduled action must not be empty",
        "NGIN.UI", "Window::Schedule", Id().c_str()));
  }

  try {
    const auto id = m_implementation->nextScheduledActionId++;
    m_implementation->scheduledActions.push_back(Implementation::ScheduledEntry{
        .id = id,
        .due = m_implementation->platform->MonotonicNow() +
               std::max(delay, std::chrono::milliseconds{0}),
        .action = std::move(action),
    });
    m_implementation->platform->WakeEventLoop();
    return id;
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeUIError(
        UIErrorCode::OutOfMemory, "Scheduled action allocation failed",
        "NGIN.UI", "Window::Schedule", Id().c_str()));
  } catch (...) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidState, "Scheduled action construction failed",
        "NGIN.UI", "Window::Schedule", Id().c_str()));
  }
}

auto Window::CancelScheduled(const ScheduledActionId id) noexcept -> bool {
  if (id == 0) {
    return false;
  }
  const auto previous = m_implementation->scheduledActions.size();
  std::erase_if(m_implementation->scheduledActions,
                [id](const Implementation::ScheduledEntry &entry) {
                  return entry.id == id;
                });
  return m_implementation->scheduledActions.size() != previous;
}

struct Application::Implementation final : IAccessibilityActionSink {
  class EventCollector final : public IPlatformEventSink {
  public:
    void Push(PlatformEvent event) override {
      events.push_back(std::move(event));
    }

    std::vector<PlatformEvent> events{};
  };

  std::unique_ptr<IPlatformBackend> platform{};
  std::unique_ptr<IRenderBackend> renderer{};
  std::unique_ptr<IAccessibilityBackend> accessibility{};
  UIAsyncScheduler asyncScheduler{};
  NGIN::Async::CancellationSource lifetimeCancellation{};
  NGIN::Async::TaskSupervisor<> asyncTasks{
      NGIN::Async::AsyncEnvironment{
          .executor = NGIN::Execution::ExecutorRef::From(asyncScheduler),
          .cancellation = lifetimeCancellation.GetToken()},
      256};

  std::vector<std::unique_ptr<Window>> windows{};
  std::mutex accessibilityMutex{};
  std::deque<AccessibilityActionRequest> accessibilityActions{};
  std::optional<UIError> accessibilityError{};
  bool exitRequested{false};

  auto PostAccessibilityAction(AccessibilityActionRequest request) noexcept
      -> UIResult<void> override {
    try {
      {
        std::scoped_lock lock{accessibilityMutex};
        accessibilityActions.push_back(std::move(request));
      }
      if (platform) {
        platform->WakeEventLoop();
      }
      return {};
    } catch (...) {
      return std::unexpected(MakeUIError(
          UIErrorCode::OutOfMemory, "The accessibility action queue is full",
          "NGIN.UI", "PostAccessibilityAction"));
    }
  }

  [[nodiscard]] auto FindWindow(const PlatformWindowHandle handle) noexcept
      -> Window * {
    const auto found =
        std::find_if(windows.begin(), windows.end(),
                     [handle](const std::unique_ptr<Window> &window) {
                       return window->PlatformHandle() == handle;
                     });
    return found == windows.end() ? nullptr : found->get();
  }
};

Application::Application(std::unique_ptr<IPlatformBackend> platform,
                         std::unique_ptr<IRenderBackend> renderer,
                         std::unique_ptr<IAccessibilityBackend> accessibility)
    : m_implementation(std::make_unique<Implementation>()) {
  m_implementation->platform = std::move(platform);
  m_implementation->asyncScheduler.Bind(*m_implementation->platform);
  m_implementation->renderer = std::move(renderer);
  m_implementation->accessibility = std::move(accessibility);
}

Application::~Application() {
  for (auto &window : m_implementation->windows) {
    window->m_implementation->lifetimeCancellation.Cancel(
        NGIN::Async::StopReason::RuntimeShutdown);
    window->ContentSurface().Close();
  }
  m_implementation->asyncTasks.Close();
  m_implementation->lifetimeCancellation.Cancel(
      NGIN::Async::StopReason::RuntimeShutdown);
  m_implementation->asyncTasks.RequestStop(
      NGIN::Async::StopReason::RuntimeShutdown);
  m_implementation->asyncScheduler.DrainReady(
      m_implementation->platform->MonotonicNow());
  if (!m_implementation->asyncTasks.TryJoin())
    std::terminate();
  m_implementation->asyncScheduler.Shutdown();
  static_cast<void>(m_implementation->renderer->WaitIdle());
  for (auto window = m_implementation->windows.rbegin();
       window != m_implementation->windows.rend(); ++window) {
    if (!(*window)->m_implementation->closed) {
      if (m_implementation->accessibility) {
        static_cast<void>(m_implementation->accessibility->DetachWindow(
            (*window)->m_implementation->platformHandle));
      }
      static_cast<void>(m_implementation->renderer->DestroySurface(
          (*window)->m_implementation->surfaceHandle));
      static_cast<void>(m_implementation->platform->DestroyWindow(
          (*window)->m_implementation->platformHandle));
      (*window)->m_implementation->closed = true;
    }
  }
}

auto Application::CreateWindow(const WindowCreateInfo &info) noexcept
    -> UIResult<Window *> {
  auto topLevelInfo = info;
  topLevelInfo.kind = WindowKind::TopLevel;
  topLevelInfo.owner = {};
  topLevelInfo.modal = false;
  return CreateWindowInternal(std::move(topLevelInfo), nullptr);
}

auto Application::CreateDialogWindow(Window &owner,
                                     const WindowCreateInfo &info,
                                     const bool modal) noexcept
    -> UIResult<DialogWindow *> {
  const auto ownedByApplication = std::any_of(
      m_implementation->windows.begin(), m_implementation->windows.end(),
      [&owner](const std::unique_ptr<Window> &candidate) {
        return candidate.get() == &owner;
      });
  if (!ownedByApplication || owner.IsClosed()) {
    return std::unexpected(
        MakeUIError(UIErrorCode::InvalidArgument,
                    "Dialog owner must be a live window in this application",
                    m_implementation->platform->Name(), "CreateDialogWindow",
                    info.id.c_str()));
  }
  if (modal && owner.ActiveModalDialog() != nullptr) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidState, "Window already owns an active modal dialog",
        m_implementation->platform->Name(), "CreateDialogWindow",
        owner.Id().c_str()));
  }

  auto dialogInfo = info;
  dialogInfo.kind = WindowKind::Dialog;
  dialogInfo.owner = owner.PlatformHandle();
  dialogInfo.modal = modal;
  auto created = CreateWindowInternal(std::move(dialogInfo), &owner);
  if (!created) {
    return std::unexpected(std::move(created).error());
  }
  return static_cast<DialogWindow *>(created.value());
}

auto Application::CreateWindowInternal(WindowCreateInfo info,
                                       Window *owner) noexcept
    -> UIResult<Window *> {
  if (info.id.Empty()) {
    return std::unexpected(
        MakeUIError(UIErrorCode::InvalidArgument, "Window id must not be empty",
                    m_implementation->platform->Name(), "CreateWindow"));
  }
  if (info.initialSize.IsEmpty()) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidArgument, "Window initial size must be non-zero",
        m_implementation->platform->Name(), "CreateWindow", info.id.c_str()));
  }

  const auto duplicate = std::find_if(
      m_implementation->windows.begin(), m_implementation->windows.end(),
      [&info](const std::unique_ptr<Window> &window) {
        return !window->IsClosed() && window->Id() == info.id;
      });
  if (duplicate != m_implementation->windows.end()) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidArgument, "A live window already uses this id",
        m_implementation->platform->Name(), "CreateWindow", info.id.c_str()));
  }

  auto platformWindow = m_implementation->platform->CreateWindow(info);
  if (!platformWindow) {
    return std::unexpected(std::move(platformWindow).error());
  }

  auto surface = m_implementation->renderer->CreateSurface(
      platformWindow.value(), info.initialSize);
  if (!surface) {
    static_cast<void>(
        m_implementation->platform->DestroyWindow(platformWindow.value()));
    return std::unexpected(std::move(surface).error());
  }

  if (info.initiallyVisible) {
    auto shown = m_implementation->platform->ShowWindow(platformWindow.value());
    if (!shown) {
      static_cast<void>(
          m_implementation->renderer->DestroySurface(surface.value()));
      static_cast<void>(
          m_implementation->platform->DestroyWindow(platformWindow.value()));
      return std::unexpected(std::move(shown).error());
    }
  }

  std::unique_ptr<Window> window;
  if (info.kind == WindowKind::Dialog && owner != nullptr) {
    window = std::unique_ptr<Window>(
        new DialogWindow{info, platformWindow.value(), surface.value(),
                         *m_implementation->platform, *owner});
  } else {
    window = std::unique_ptr<Window>(
        new Window{info, platformWindow.value(), surface.value(),
                   *m_implementation->platform, nullptr});
  }
  auto *result = window.get();
  m_implementation->windows.push_back(std::move(window));
  if (m_implementation->accessibility) {
    auto nativeWindow =
        m_implementation->platform->QueryNativeWindow(platformWindow.value());
    if (!nativeWindow) {
      static_cast<void>(
          m_implementation->renderer->DestroySurface(surface.value()));
      static_cast<void>(
          m_implementation->platform->DestroyWindow(platformWindow.value()));
      m_implementation->windows.pop_back();
      return std::unexpected(std::move(nativeWindow).error());
    }
    auto attached =
        m_implementation->accessibility->AttachWindow(AccessibilityWindowInfo{
            .window = platformWindow.value(),
            .nativeWindow = nativeWindow.value(),
            .title = info.title,
        });
    if (!attached) {
      static_cast<void>(
          m_implementation->renderer->DestroySurface(surface.value()));
      static_cast<void>(
          m_implementation->platform->DestroyWindow(platformWindow.value()));
      m_implementation->windows.pop_back();
      return std::unexpected(std::move(attached).error());
    }
    auto published =
        m_implementation->accessibility->Publish(AccessibilitySnapshot{
            .window = platformWindow.value(),
            .revision = result->ContentSurface().SemanticRevision(),
            .root = result->Semantics().Root(),
            .nodes = result->Semantics().Nodes(),
        });
    if (!published) {
      static_cast<void>(m_implementation->accessibility->DetachWindow(
          platformWindow.value()));
      static_cast<void>(
          m_implementation->renderer->DestroySurface(surface.value()));
      static_cast<void>(
          m_implementation->platform->DestroyWindow(platformWindow.value()));
      m_implementation->windows.pop_back();
      return std::unexpected(std::move(published).error());
    }
  }
  if (owner != nullptr && info.modal) {
    result->m_implementation->ownerFocusToRestore = owner->FocusedElement();
    owner->m_implementation->activeModalDialog =
        static_cast<DialogWindow *>(result);
  }
  return result;
}

auto Application::CloseWindow(Window &window) noexcept -> UIResult<void> {
  if (window.m_implementation->closed) {
    return {};
  }

  std::vector<Window *> ownedWindows;
  for (const auto &candidate : m_implementation->windows) {
    if (!candidate->IsClosed() &&
        candidate->m_implementation->owner == &window) {
      ownedWindows.push_back(candidate.get());
    }
  }
  for (auto *owned : ownedWindows) {
    auto closed = CloseWindow(*owned);
    if (!closed) {
      return std::unexpected(std::move(closed).error());
    }
  }

  if (m_implementation->accessibility) {
    auto detached = m_implementation->accessibility->DetachWindow(
        window.m_implementation->platformHandle);
    if (!detached) {
      m_implementation->accessibilityError = detached.error();
    }
  }

  static_cast<void>(window.ContentSurface().Route(
      WindowFocusChanged{.window = window.PlatformHandle(), .focused = false}));
  auto destroyedSurface = m_implementation->renderer->DestroySurface(
      window.m_implementation->surfaceHandle);
  if (!destroyedSurface) {
    return std::unexpected(std::move(destroyedSurface).error());
  }

  auto destroyedWindow = m_implementation->platform->DestroyWindow(
      window.m_implementation->platformHandle);
  if (!destroyedWindow) {
    return std::unexpected(std::move(destroyedWindow).error());
  }

  window.ContentSurface().Close();
  window.m_implementation->lifetimeCancellation.Cancel(
      NGIN::Async::StopReason::OwnerClosed);
  window.m_implementation->closed = true;
  window.m_implementation->scheduledActions.clear();
  if (auto *owner = window.m_implementation->owner;
      owner != nullptr && !owner->IsClosed() &&
      owner->m_implementation->activeModalDialog == &window) {
    owner->m_implementation->activeModalDialog = nullptr;
    static_cast<void>(
        owner->Focus(window.m_implementation->ownerFocusToRestore));
    owner->Invalidate(InvalidationKind::Paint | InvalidationKind::Semantics);
  }
  return {};
}

auto Application::PumpOnce(const std::chrono::milliseconds maximumWait) noexcept
    -> UIResult<void> {
  auto effectiveWait = std::max(maximumWait, std::chrono::milliseconds{0});
  const auto beforeWait = m_implementation->platform->MonotonicNow();
  if (const auto deadline = m_implementation->asyncScheduler.NextDeadline()) {
    if (*deadline == MonotonicTime{0} || *deadline <= beforeWait) {
      effectiveWait = std::chrono::milliseconds{0};
    } else {
      effectiveWait = std::min(
          effectiveWait,
          std::chrono::ceil<std::chrono::milliseconds>(*deadline - beforeWait));
    }
  }
  for (const auto &window : m_implementation->windows) {
    if (window->IsClosed()) {
      continue;
    }
    if (window->IsDirty()) {
      effectiveWait = std::chrono::milliseconds{0};
    }
    if (const auto deadline = window->NextAnimationDeadline()) {
      if (*deadline <= beforeWait) {
        effectiveWait = std::chrono::milliseconds{0};
      } else {
        effectiveWait = std::min(effectiveWait,
                                 std::chrono::ceil<std::chrono::milliseconds>(
                                     *deadline - beforeWait));
      }
    }
    for (const auto &entry : window->m_implementation->scheduledActions) {
      if (entry.due <= beforeWait) {
        effectiveWait = std::chrono::milliseconds{0};
        break;
      }
      const auto untilDue =
          std::chrono::ceil<std::chrono::milliseconds>(entry.due - beforeWait);
      effectiveWait = std::min(effectiveWait, untilDue);
    }
  }

  Implementation::EventCollector collector;
  auto eventResult =
      effectiveWait.count() > 0
          ? m_implementation->platform->WaitEvents(collector, effectiveWait)
          : m_implementation->platform->PollEvents(collector);
  if (!eventResult) {
    return std::unexpected(std::move(eventResult).error());
  }

  std::deque<AccessibilityActionRequest> accessibilityActions;
  {
    std::scoped_lock lock{m_implementation->accessibilityMutex};
    accessibilityActions.swap(m_implementation->accessibilityActions);
  }
  for (const auto &request : accessibilityActions) {
    auto *window = m_implementation->FindWindow(request.window);
    if (window == nullptr || window->IsClosed()) {
      m_implementation->accessibilityError =
          MakeUIError(UIErrorCode::InvalidState,
                      "Accessibility action targeted a destroyed window",
                      "NGIN.UI", "DispatchAccessibilityAction");
      continue;
    }
    auto performed = window->PerformSemanticAction(request.semantic);
    if (!performed) {
      m_implementation->accessibilityError = performed.error();
    }
  }

  const auto afterWait = m_implementation->platform->MonotonicNow();
  m_implementation->asyncScheduler.RunReady(afterWait);
  for (auto &window : m_implementation->windows) {
    if (window->IsClosed()) {
      continue;
    }
    std::vector<Window::ScheduledAction> dueActions;
    auto &entries = window->m_implementation->scheduledActions;
    for (auto &entry : entries) {
      if (entry.due <= afterWait) {
        dueActions.push_back(std::move(entry.action));
      }
    }
    std::erase_if(entries, [afterWait](const auto &entry) {
      return entry.due <= afterWait;
    });
    for (auto &action : dueActions) {
      try {
        action();
      } catch (const std::bad_alloc &) {
        return std::unexpected(MakeUIError(
            UIErrorCode::OutOfMemory, "Scheduled UI action allocation failed",
            "NGIN.UI", "Window::Schedule", window->Id().c_str()));
      } catch (...) {
        return std::unexpected(MakeUIError(
            UIErrorCode::InvalidState, "Scheduled UI action threw an exception",
            "NGIN.UI", "Window::Schedule", window->Id().c_str()));
      }
    }
  }

  for (const auto &event : collector.events) {
    const auto handle = EventWindow(event);
    if (!handle) {
      for (auto &window : m_implementation->windows) {
        if (!window->IsClosed()) {
          window->Invalidate();
          if (window->m_implementation->eventHandler) {
            window->m_implementation->eventHandler(event);
          }
        }
      }
      continue;
    }

    auto *window = m_implementation->FindWindow(handle);
    if (window == nullptr || window->IsClosed()) {
      continue;
    }
    if (window->ActiveModalDialog() != nullptr && IsModalBlockedEvent(event)) {
      continue;
    }

    if (window->m_implementation->eventHandler) {
      window->m_implementation->eventHandler(event);
    }

    static_cast<void>(window->ContentSurface().Route(event));

    if (const auto *resized = std::get_if<WindowResized>(&event)) {
      if (!resized->size.IsEmpty()) {
        auto resizedSurface = m_implementation->renderer->ResizeSurface(
            window->m_implementation->surfaceHandle, resized->size);
        if (!resizedSurface) {
          return std::unexpected(std::move(resizedSurface).error());
        }
        window->m_implementation->pixelExtent = resized->size;
      }
      window->Invalidate(InvalidationKind::Measure | InvalidationKind::Arrange |
                         InvalidationKind::Paint);
    } else if (const auto *scale = std::get_if<WindowScaleChanged>(&event)) {
      if (scale->scaleFactor > 0.0F) {
        window->m_implementation->scaleFactor = scale->scaleFactor;
      }
      window->Invalidate(InvalidationKind::Measure | InvalidationKind::Arrange |
                         InvalidationKind::Paint);
    } else if (std::holds_alternative<WindowCloseRequested>(event)) {
      window->m_implementation->closeRequested = true;
    } else if (!std::holds_alternative<PointerMoved>(event) &&
               !std::holds_alternative<PointerButtonChanged>(event) &&
               !std::holds_alternative<PointerWheelChanged>(event) &&
               !std::holds_alternative<WindowFocusChanged>(event)) {
      window->Invalidate();
    }
  }

  for (auto &window : m_implementation->windows) {
    if (window->IsClosed()) {
      continue;
    }
    if (window->IsCloseRequested()) {
      auto closed = CloseWindow(*window);
      if (!closed) {
        return std::unexpected(std::move(closed).error());
      }
      continue;
    }
    if (!window->ContentSurface().NeedsUpdate(afterWait)) {
      continue;
    }

    const auto frameStarted = DiagnosticsClock::now();
    auto &surface = window->ContentSurface();
    const auto previousRevision = surface.SemanticRevision();
    auto frame =
        surface.Update(window->PixelExtent(), window->ScaleFactor(), afterWait);
    if (window->IsClosed())
      continue;
    if (!frame) {
      return std::unexpected(std::move(frame).error());
    }
    auto frameTimings = surface.Diagnostics().frameTimings;
    if (surface.SemanticRevision() != previousRevision &&
        m_implementation->accessibility) {
      try {
        SemanticNodeId focused{};
        if (const auto *focusedRuntime =
                window->Tree().Get(window->FocusedElement());
            focusedRuntime != nullptr) {
          if (const auto *focusedSemantic =
                  window->Semantics().FindByOwner(focusedRuntime->id);
              focusedSemantic != nullptr) {
            focused = focusedSemantic->id;
          }
        }
        auto published =
            m_implementation->accessibility->Publish(AccessibilitySnapshot{
                .window = window->PlatformHandle(),
                .revision = surface.SemanticRevision(),
                .root = window->Semantics().Root(),
                .focused = focused,
                .nodes = window->Semantics().Nodes(),
            });
        if (!published) {
          m_implementation->accessibilityError = published.error();
        }
      } catch (...) {
        m_implementation->accessibilityError =
            MakeUIError(UIErrorCode::OutOfMemory,
                        "Accessibility snapshot allocation failed", "NGIN.UI",
                        "PublishAccessibilitySnapshot", window->Id().c_str());
      }
    }

    const auto renderStarted = DiagnosticsClock::now();
    auto rendered = m_implementation->renderer->Render(window->SurfaceHandle(),
                                                       frame.value()->View());
    if (!rendered) {
      return std::unexpected(std::move(rendered).error());
    }
    frameTimings.renderMilliseconds =
        ElapsedMilliseconds(renderStarted, DiagnosticsClock::now());
    const auto presentStarted = DiagnosticsClock::now();
    auto presented =
        m_implementation->renderer->Present(window->SurfaceHandle());
    if (!presented) {
      return std::unexpected(std::move(presented).error());
    }
    frameTimings.presentMilliseconds =
        ElapsedMilliseconds(presentStarted, DiagnosticsClock::now());
    frameTimings.totalMilliseconds =
        ElapsedMilliseconds(frameStarted, DiagnosticsClock::now());
    surface.RecordPresentation(frameTimings);
  }

  return {};
}

auto Application::Run() noexcept -> UIResult<void> {
  while (!ShouldExit()) {
    auto pumped = PumpOnce(std::chrono::milliseconds{250});
    if (!pumped) {
      static_cast<void>(ShutdownTasks());
      return pumped;
    }
  }
  return ShutdownTasks();
}

auto Application::BackgroundTasks() noexcept
    -> NGIN::Async::TaskSupervisor<> & {
  return m_implementation->asyncTasks;
}

auto Application::ShutdownTasks() noexcept -> UIResult<void> {
  auto &owner = m_implementation->asyncTasks;
  owner.Close();
  m_implementation->lifetimeCancellation.Cancel(
      NGIN::Async::StopReason::RuntimeShutdown);
  owner.RequestStop(NGIN::Async::StopReason::RuntimeShutdown);
  for (;;) {
    if (auto report = owner.TryJoin()) {
      if (report->HasError())
        return NGIN::Utilities::Unexpected<UIError>(
            MakeUIError(UIErrorCode::InvalidState,
                        "An application task failed; inspect "
                        "BackgroundTasks().TakeResult()",
                        "NGIN.UI", "ShutdownTasks"));
      return {};
    }
    auto pumped = PumpOnce(std::chrono::milliseconds{250});
    if (!pumped)
      return pumped;
  }
}

void Application::RequestExit() noexcept {
  m_implementation->exitRequested = true;
  m_implementation->platform->WakeEventLoop();
}

void Application::InvalidateAll(const InvalidationKind kind) noexcept {
  for (auto &window : m_implementation->windows) {
    if (!window->IsClosed()) {
      window->Invalidate(kind);
    }
  }
}

auto Application::ShouldExit() const noexcept -> bool {
  return m_implementation->exitRequested || ActiveWindowCount() == 0;
}

auto Application::ActiveWindowCount() const noexcept -> UIntSize {
  return static_cast<UIntSize>(std::count_if(
      m_implementation->windows.begin(), m_implementation->windows.end(),
      [](const std::unique_ptr<Window> &window) {
        return !window->IsClosed();
      }));
}

auto Application::Platform() noexcept -> IPlatformBackend & {
  return *m_implementation->platform;
}

auto Application::Renderer() noexcept -> IRenderBackend & {
  return *m_implementation->renderer;
}

auto Application::CreateTaskContext(
    NGIN::Async::CancellationToken cancellation) noexcept
    -> NGIN::Async::TaskContext {
  auto context = NGIN::Async::TaskContext{
      m_implementation->asyncScheduler,
      m_implementation->lifetimeCancellation.GetToken()};
  context.BindLinkedCancellationToken(std::move(cancellation));
  return context;
}

auto Application::CreateTaskContext(
    Window &window, NGIN::Async::CancellationToken cancellation) noexcept
    -> NGIN::Async::TaskContext {
  auto context = CreateTaskContext(std::move(cancellation));
  const auto owned =
      std::ranges::any_of(m_implementation->windows,
                          [&window](const std::unique_ptr<Window> &candidate) {
                            return candidate.get() == &window;
                          });
  if (!owned) {
    NGIN::Async::CancellationSource invalidOwner;
    invalidOwner.Cancel(NGIN::Async::StopReason::OwnerClosed);
    context.BindLinkedCancellationToken(invalidOwner.GetToken());
    return context;
  }
  context.BindLinkedCancellationToken(
      window.m_implementation->lifetimeCancellation.GetToken());
  return context;
}

auto Application::AccessibilityDiagnostics() const noexcept
    -> NGIN::UI::AccessibilityDiagnostics {
  if (!m_implementation->accessibility) {
    return {};
  }
  auto diagnostics = m_implementation->accessibility->Diagnostics();
  if (m_implementation->accessibilityError) {
    diagnostics.lastError = m_implementation->accessibilityError;
  }
  return diagnostics;
}

auto CreateApplication(ApplicationCreateInfo info) noexcept
    -> UIResult<std::unique_ptr<Application>> {
  if (!info.platform) {
    return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                                       "Platform backend is required", "",
                                       "CreateApplication"));
  }
  if (!info.renderer) {
    return std::unexpected(MakeUIError(UIErrorCode::InvalidArgument,
                                       "Renderer backend is required", "",
                                       "CreateApplication"));
  }
  if (!info.platform->ContractVersion().Supports(
          CurrentBackendContractVersion)) {
    return std::unexpected(
        MakeUIError(UIErrorCode::Unsupported,
                    "Platform backend contract version is incompatible",
                    info.platform->Name(), "ValidateBackendContract"));
  }
  if (!info.renderer->ContractVersion().Supports(
          CurrentBackendContractVersion)) {
    return std::unexpected(
        MakeUIError(UIErrorCode::Unsupported,
                    "Renderer backend contract version is incompatible",
                    info.renderer->Name(), "ValidateBackendContract"));
  }
  if (!HasPlatformCapability(info.platform->Capabilities(),
                             RequiredPlatformCapabilities)) {
    return std::unexpected(MakeUIError(
        UIErrorCode::Unsupported,
        "Platform backend lacks required multiple-window capability",
        info.platform->Name(), "ValidateBackendCapabilities"));
  }
  if (!HasRenderCapability(info.renderer->Capabilities(),
                           RequiredRenderCapabilities)) {
    return std::unexpected(MakeUIError(
        UIErrorCode::Unsupported,
        "Renderer backend lacks required scissor or 32-bit index capability",
        info.renderer->Name(), "ValidateBackendCapabilities"));
  }

  auto platformInitialized = info.platform->Initialize(
      PlatformInitInfo{.applicationName = info.applicationName});
  if (!platformInitialized) {
    return std::unexpected(std::move(platformInitialized).error());
  }

  auto rendererInitialized = info.renderer->Initialize(
      RenderInitInfo{.enableValidation = info.enableRendererValidation});
  if (!rendererInitialized) {
    return std::unexpected(std::move(rendererInitialized).error());
  }

  auto application = std::unique_ptr<Application>(
      new Application{std::move(info.platform), std::move(info.renderer),
                      std::move(info.accessibility)});
  if (application->m_implementation->accessibility) {
    auto accessibilityInitialized =
        application->m_implementation->accessibility->Initialize(
            *application->m_implementation);
    if (!accessibilityInitialized) {
      return std::unexpected(std::move(accessibilityInitialized).error());
    }
  }
  return application;
}
} // namespace NGIN::UI
