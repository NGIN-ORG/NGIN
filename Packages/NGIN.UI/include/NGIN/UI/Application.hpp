#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/UI/Accessibility.hpp>
#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Diagnostics.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Events.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Inspector.hpp>
#include <NGIN/UI/Invalidation.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Platform.hpp>
#include <NGIN/UI/Rendering.hpp>
#include <NGIN/UI/RuntimeTree.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/UIRenderer.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <chrono>
#include <memory>
#include <optional>

namespace NGIN::UI {
/// @brief Owns the UI event loop, platform backend, renderer, and windows.
class Application;
/// @brief A child window that can optionally block interaction with its owner.
class DialogWindow;

/// @brief Hosts, composes, lays out, renders, and dispatches input to one UI
/// tree.
class Window {
public:
  using EventHandler = NGIN::Utilities::Callable<void(const PlatformEvent &)>;
  using Content = NGIN::Utilities::Callable<void(Composer &)>;
  using ScheduledAction = NGIN::Utilities::Callable<void()>;
  using ScheduledActionId = UInt64;

  Window(const Window &) = delete;
  Window(Window &&) = delete;
  auto operator=(const Window &) -> Window & = delete;
  auto operator=(Window &&) -> Window & = delete;
  virtual ~Window();

  [[nodiscard]] auto Id() const noexcept -> const NGIN::Text::String &;
  [[nodiscard]] auto PlatformHandle() const noexcept -> PlatformWindowHandle;
  [[nodiscard]] auto SurfaceHandle() const noexcept -> RenderSurfaceHandle;
  [[nodiscard]] auto PixelExtent() const noexcept -> PixelSize;
  [[nodiscard]] auto ScaleFactor() const noexcept -> F32;
  [[nodiscard]] auto IsDirty() const noexcept -> bool;
  [[nodiscard]] auto IsClosed() const noexcept -> bool;
  [[nodiscard]] auto IsCloseRequested() const noexcept -> bool;
  [[nodiscard]] auto Kind() const noexcept -> WindowKind;
  [[nodiscard]] auto IsModal() const noexcept -> bool;
  [[nodiscard]] auto Owner() const noexcept -> const Window *;
  [[nodiscard]] auto ActiveModalDialog() const noexcept -> const DialogWindow *;
  [[nodiscard]] auto Tree() const noexcept -> const RuntimeTree &;
  [[nodiscard]] auto LastReconcileStats() const noexcept
      -> const ReconcileStats &;
  [[nodiscard]] auto LastLayoutStats() const noexcept
      -> const LayoutPassStats &;
  [[nodiscard]] auto DisplayCommandCount() const noexcept -> UIntSize;
  [[nodiscard]] auto HasActiveAnimations() const noexcept -> bool;
  [[nodiscard]] auto NextAnimationDeadline() const noexcept
      -> std::optional<MonotonicTime>;
  [[nodiscard]] auto MotionEnabled() const noexcept -> bool;
  [[nodiscard]] auto HitTest(Point position) const noexcept -> ElementHandle;
  [[nodiscard]] auto FocusedElement() const noexcept -> ElementHandle;
  [[nodiscard]] auto CapturedElement(UInt64 pointerId) const noexcept
      -> ElementHandle;
  [[nodiscard]] auto Semantics() const noexcept -> const SemanticTree &;
  [[nodiscard]] auto Diagnostics() const noexcept -> const WindowDiagnostics &;
  [[nodiscard]] auto Inspect() const -> InspectorSnapshot;
  [[nodiscard]] auto InspectorOverlay() const noexcept
      -> const InspectorOverlayOptions &;

  auto Focus(ElementHandle handle) noexcept -> bool;
  auto FocusNext(bool reverse = false) -> bool;
  auto PerformSemanticAction(const SemanticActionRequest &request) noexcept
      -> UIResult<void>;
  void SetInspectorOverlay(InspectorOverlayOptions options);
  void SetEventHandler(EventHandler handler);
  void SetContent(Content content);
  void Invalidate(InvalidationKind kind = InvalidationKind::All) noexcept;
  void SetMotionEnabled(bool enabled) noexcept;
  [[nodiscard]] auto Schedule(std::chrono::milliseconds delay,
                              ScheduledAction action) noexcept
      -> UIResult<ScheduledActionId>;
  [[nodiscard]] auto CancelScheduled(ScheduledActionId id) noexcept -> bool;

private:
  friend class Application;
  friend class DialogWindow;

  explicit Window(WindowCreateInfo info, PlatformWindowHandle platformHandle,
                  RenderSurfaceHandle surfaceHandle,
                  IPlatformBackend &platformBackend, Window *owner);

  struct Implementation;
  std::unique_ptr<Implementation> m_implementation;
};

/// @brief A window associated with an owner and optionally shown modally.
class DialogWindow final : public Window {
private:
  friend class Application;

  explicit DialogWindow(WindowCreateInfo info,
                        PlatformWindowHandle platformHandle,
                        RenderSurfaceHandle surfaceHandle,
                        IPlatformBackend &platformBackend, Window &owner);
};

/// @brief Backends and identity used to create an Application.
struct ApplicationCreateInfo final {
  std::unique_ptr<IPlatformBackend> platform{};
  std::unique_ptr<IRenderBackend> renderer{};
  NGIN::Text::String applicationName{"NGIN.UI"};
  bool enableRendererValidation{false};
  std::unique_ptr<IAccessibilityBackend> accessibility{};
};

/// @brief Coordinates backend lifetime, window creation, and the application
/// loop.
class Application final {
public:
  Application(const Application &) = delete;
  Application(Application &&) = delete;
  auto operator=(const Application &) -> Application & = delete;
  auto operator=(Application &&) -> Application & = delete;
  ~Application();

  [[nodiscard]] auto CreateWindow(const WindowCreateInfo &info) noexcept
      -> UIResult<Window *>;
  [[nodiscard]] auto CreateDialogWindow(Window &owner,
                                        const WindowCreateInfo &info,
                                        bool modal = true) noexcept
      -> UIResult<DialogWindow *>;
  auto CloseWindow(Window &window) noexcept -> UIResult<void>;
  auto PumpOnce(std::chrono::milliseconds maximumWait =
                    std::chrono::milliseconds{0}) noexcept -> UIResult<void>;
  auto Run() noexcept -> UIResult<void>;
  void RequestExit() noexcept;
  /// @brief Marks every open window dirty for the requested phases.
  void InvalidateAll(InvalidationKind kind = InvalidationKind::All) noexcept;

  [[nodiscard]] auto ShouldExit() const noexcept -> bool;
  [[nodiscard]] auto ActiveWindowCount() const noexcept -> UIntSize;
  [[nodiscard]] auto Platform() noexcept -> IPlatformBackend &;
  [[nodiscard]] auto Renderer() noexcept -> IRenderBackend &;
  /// @brief Creates a UI-scheduled task context canceled with the application.
  ///
  /// The application drains active tasks during shutdown. Do not schedule new
  /// work through this context after the application has been destroyed.
  [[nodiscard]] auto
  CreateTaskContext(NGIN::Async::CancellationToken cancellation = {}) noexcept
      -> NGIN::Async::TaskContext;
  [[nodiscard]] auto AccessibilityDiagnostics() const noexcept
      -> NGIN::UI::AccessibilityDiagnostics;

private:
  friend auto CreateApplication(ApplicationCreateInfo info) noexcept
      -> UIResult<std::unique_ptr<Application>>;

  Application(std::unique_ptr<IPlatformBackend> platform,
              std::unique_ptr<IRenderBackend> renderer,
              std::unique_ptr<IAccessibilityBackend> accessibility);
  [[nodiscard]] auto CreateWindowInternal(WindowCreateInfo info,
                                          Window *owner) noexcept
      -> UIResult<Window *>;

  struct Implementation;
  std::unique_ptr<Implementation> m_implementation;
};

[[nodiscard]] auto CreateApplication(ApplicationCreateInfo info) noexcept
    -> UIResult<std::unique_ptr<Application>>;
} // namespace NGIN::UI
