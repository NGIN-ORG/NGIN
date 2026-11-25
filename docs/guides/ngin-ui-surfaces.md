# Components, layout, and retained surfaces

`NGIN.UI` owns the component API, runtime tree, layout algorithms, input,
semantics, and preparation of draw data. These layers ship in one core library.
The host supplies the rendering device and decides when and where to present.
`NGIN.UI.Hosting` remains an optional adapter for NGIN.Core applications.

## Authoring a view

Use `Views::Column`, `Row`, `Label`, `Button`, `TextField`, and `Slider` to
compose ordinary C++ functions. Pass a typed binding for editable values,
a value for a snapshot, and a callable for an action. Text and theme services
come from the mount. A named control contributes its accessible role and label.

Wrap derived content in `Views::Component`. Observable reads made while its
factory runs subscribe that mounted instance. A write schedules composition
on the owning surface. Group related writes with `StateBatch`. Keep state
writes on the UI thread. Constructing a label from a state value outside a
component takes a snapshot. An arbitrary C++ field needs an observable
adapter that publishes changes, or a fresh view description when it changes.

Store local state with `scope.Local("name", initial)`. Apply `.Key(stableId)`
to children in reorderable lists. Keep a local state's type and key stable
for its lifetime. Two mounts of the same description own separate local state.
Shared screen state belongs in an ordinary model supplied by its caller. Use
`AsyncCommand` or `ViewModelTaskScope` for supervised work in an application.
A component that starts asynchronous work can pass `scope.Lifetime()` to its
supervised operation. Unmounting requests cancellation with `OwnerClosed`.
Capture the token and the local binding by value, dispatch completion to the
owning UI thread, and check the token before applying the result. A binding to
a retired local keeps its last readable value and rejects writes with
`InvalidState`. Reopening the component creates a new token and new local
storage. The application or view-model task supervisor owns completion and
joins outstanding work during shutdown.

For example, this reusable control owns its open state. Two instances can
expand independently. The click handler captures a checked binding by value.

```cpp
auto Details(NGIN::Text::String body) -> NGIN::UI::Views::View {
  using namespace NGIN::UI::Views;
  return Component([body = std::move(body)](ComponentScope &scope) {
    auto open = scope.Local("open", false);
    return Column({
        Button(open.Get() ? "Hide details" : "Show details", [open] {
          static_cast<void>(open.Set(!open.Get()));
        }),
        open.Get() ? Label(body) : View{},
    });
  });
}
```

## Extending the controls

* Compose controls to make a settings field or an inspector section.
* Implement `ILayoutPolicy` for a custom container. The parent measures its
  children under constraints and arranges them in its content box. The runtime
  owns the children and uses the resulting bounds for drawing, hit testing,
  and accessibility. `Views::SplitPane` is a two-child example.
* Implement `ICustomElement` and pass it to `Views::Custom` for a painted leaf.
  Measurement runs in logical units. Painting records clipped drawing commands.
  Supply semantics for information that a user would otherwise get visually.

## Driving a surface

A `Surface` belongs to the host's UI thread. A desktop window exposes its
instance through `Window::ContentSurface()`. An engine constructs one and
provides a text environment backed by its renderer. `Surface::SetContent`
accepts a borrowed environment or a shared native text service.

1. Forward input in **surface-local logical coordinates** through `Route`.
   Supply the optional platform and window in `SurfaceOptions` for native
   clipboard, cursor, and IME facilities. On input handoff, send
   `WindowFocusChanged{window, false}` to release focus and pointer capture.
2. Call `Update(pixelExtent, scale, monotonicTime)` with the target's current
   pixel dimensions. Skip a minimized or zero-sized target. Device scale maps
   logical coordinates to pixels. Drive animations with increasing host time.
3. Submit the returned frame's `View()` in the host's render pass.
   The shared frame owns its geometry. Keep the frame alive until the renderer
   finishes consuming those spans. Texture handles refer to the originating
   backend. Keep their resources alive through submission, and invalidate the
   surface after resource loss or atlas changes.
4. Present once after the scene and UI have both drawn. For an on-demand host,
   consult `IsDirty()` and `NextAnimationDeadline()`. A continuously rendering
   engine can call `Update` each tick and receive the cached packet when idle.

SDL integrations can call `SDL3::CreateOverlayBackend(renderer)` and draw into
the renderer's current target using its pixel-coordinate viewport. The adapter
preserves the scene and restores the host clip and blend state. Destroy it
before the host renderer. A custom engine can consume `RenderPacket` directly.
Use its premultiplied vertex colors, ordered batches, texture handles, index
ranges, and pixel scissors. Desktop windows use the clear load action. Embedded
surfaces request preservation of the target's contents.

## Migrating an existing screen

Keep the existing model and return a view from each screen. Register each page
with a factory that accepts the view model and navigation parameter. Mount
`navigation.ContentView()` once with `Window::SetContent` or `Surface::SetContent`. The navigation service
retains keyed pages across back navigation and observes changes to the stack.
Calling `SetContent` again replaces the mount, releases focus and local state,
and cancels the old component lifetime. Use observable writes for routine updates.

The Gallery uses this contract for its shell, searchable sidebar, and every
registered page. The property editor uses reflected metadata to choose a
bound control. Its view runs in both desktop hosts.
[Gallery property editor](../../Examples/NGIN.UI.Gallery/src/GalleryPropertyEditor.cpp)

Backend providers targeting contract 1.3 must implement the clear and preserve
load actions. The SDL backend accepts preserving packets on borrowed targets.
Applications using the standard window backend receive clear packets automatically.

## Verification and diagnosis

`Surface::Diagnostics()` reports stage counts and timings, reconciliation,
measurement, draw batches, and input ownership. Read the semantic tree to
exercise a control by role and name. Use `Testing::SoftwareRenderBackend` to
verify the composed pixels without a GPU or window system.

[Settings panel in both hosts](../../Examples/NGIN.UI.Settings/) ·
[Custom control contracts](ngin-ui-custom-controls.md) ·
[View-model lifetime](ngin-ui-viewmodel-lifetime.md)
