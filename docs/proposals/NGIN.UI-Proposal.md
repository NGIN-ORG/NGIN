# NGIN.UI Architecture and Implementation Proposal

**Status:** Draft proposal
**Target:** NGIN ecosystem, C++23
**Initial platforms:** Windows, Linux, macOS
**Initial reference backend:** SDL3 + SDL_GPU
**Intended repository location:** `docs/proposals/NGIN.UI-Proposal.md`

---

## 1. Executive decision

`NGIN.UI` should be a backend-neutral, general-purpose application UI toolkit.

It should **not** directly implement Win32, Cocoa, Wayland, Vulkan, Direct3D, Metal, or SDL inside the core library. It should define the narrow contracts it needs from:

1. a **platform backend** for event dispatch, OS windows, input, cursors, clipboard, DPI, monitors, drag-and-drop, and IME; and
2. a **renderer backend** for render targets, textures, frame submission, clipping, and presentation.

Concrete implementations should be separate packages.

The architecture should borrow two important ideas from Dear ImGui:

- application code reissues a description of the UI instead of owning a tree of native widget objects;
- platform and renderer integration are independent backends.

It should **not** copy Dear ImGui’s developer-tool-oriented limitations. NGIN.UI is intended for ordinary desktop and, later, mobile applications. It therefore also needs:

- automatic layout;
- event-driven invalidation rather than mandatory continuous rendering;
- retained focus, text editing, scrolling, animation, and accessibility state;
- multiple real OS windows;
- semantic controls;
- typed styling and themes;
- Unicode shaping and editing;
- testable headless composition;
- application lifecycle integration.

The recommended model is:

> **Declarative at the call site, immediate during composition, retained where state and performance require it.**

Application code describes the current UI. NGIN.UI composes that description into a retained runtime tree, performs layout, produces a device-independent display list, and passes that display list to a renderer backend.

---

## 2. Relationship to the existing NGIN architecture

NGIN already separates build tooling from the optional `NGIN.Core` hosted runtime. NGIN.UI should preserve that separation.

The dependency direction should be:

```text
NGIN.Base
    ↑
NGIN.UI
    ↑
NGIN.UI.Backend.*
```

Optional hosted integration should be:

```text
NGIN.Base
   ↑       ↑
NGIN.UI   NGIN.Core
   \       /
    \     /
 NGIN.UI.Hosting
```

The core UI package must be usable by a plain native executable that does not link `NGIN.Core`.

`NGIN.UI.Hosting` should integrate with:

- `NGIN.Core` lifecycle;
- service registration;
- task scheduling;
- configuration;
- logging;
- module-owned resources;
- plugins.

NGIN.UI must not create a second service container, module system, logger, configuration system, or general-purpose task runtime.

---

## 3. Goals

### 3.1 Primary goals

NGIN.UI should:

- provide a strongly typed C++23 UI API;
- support ordinary desktop applications, not only tools and overlays;
- keep application state in application-owned models;
- avoid raw ownership of widget objects in application code;
- support native OS windows through replaceable platform backends;
- support replaceable rendering backends;
- provide consistent cross-platform controls by default;
- allow native and custom-rendered escape hatches;
- support event-driven rendering and efficient idle behavior;
- provide deterministic, headless testing;
- integrate naturally with NGIN packages and `NGIN.Core`;
- have a small, comprehensible initial implementation;
- allow future evolution toward mobile without requiring mobile in version 0.1.

### 3.2 Developer-experience goals

The API should feel familiar to someone coming from MAUI, SwiftUI, Compose, or Flutter, while remaining idiomatic C++.

Desired characteristics:

- hierarchy is visually obvious;
- bindings are typed;
- no string property paths;
- no required macros;
- no required reflection;
- no required markup language;
- no global singleton context in the public API;
- no explicit `new` or `delete`;
- no parent pointer management;
- no fragile `Begin`/`End` pairing in normal application code;
- normal C++ conditionals and loops remain usable;
- error messages remain understandable;
- templates are used where they add value, not as a goal themselves.

---

## 4. Non-goals for version 0.1

Version 0.1 should not attempt to provide:

- a complete general-purpose 3D graphics engine;
- a broad cross-platform operating-system abstraction;
- a browser engine;
- a full CSS implementation;
- XAML compatibility;
- native controls for every widget;
- a visual designer;
- hot reload;
- a plugin-stable binary widget ABI;
- mobile platform support;
- advanced rich text editing;
- docking;
- data-grid virtualization;
- accessibility implementations on every platform;
- every possible layout system;
- an animation authoring framework;
- remote rendering.

The architecture should not prevent these features, but they must not block the first working vertical slice.

---

## 5. Design principles

### 5.1 Backends are replaceable

The UI core knows abstract platform and rendering capabilities. It does not include backend headers.

### 5.2 Application models are the source of truth

Application data does not live inside widget instances. Controls receive values, bindings, and callbacks.

### 5.3 Runtime state is retained by NGIN.UI

NGIN.UI retains state that belongs to interaction and presentation:

- focus;
- pointer capture;
- hover and press state;
- text cursor and selection;
- composition/IME state;
- scroll offsets;
- animation progress;
- layout and text caches;
- accessibility nodes;
- renderer resources.

### 5.4 UI composition is repeatable

Calling a view function twice with the same inputs should describe the same semantic UI.

### 5.5 Identity is explicit when structure is dynamic

Static child order may use positional identity. Dynamic collections and movable branches require stable keys.

### 5.6 Rendering is invalidation-driven

An idle application should not consume a CPU core rebuilding the UI at 60 or 144 frames per second.

### 5.7 Semantics are part of the core model

Buttons, headings, editable text, labels, values, selected state, and actions must be represented semantically even before platform accessibility bridges are implemented.

### 5.8 The first backend should optimize learning

The first backend should minimize platform work so development can focus on the UI architecture. SDL3 + SDL_GPU is the recommended initial backend.

---

## 6. High-level architecture

```text
Application model
       │
       │ values, bindings, callbacks
       ▼
View composition
       │
       │ node declarations
       ▼
Reconciler / runtime tree
       │
       ├── semantic tree
       ├── focus/input state
       ├── local control state
       └── cached measurements
       │
       ▼
Layout
       │
       ▼
Display list
       │
       ▼
Renderer backend
       │
       ▼
Native render surface
       │
       ▼
OS window
```

Input travels in the opposite direction:

```text
Operating system
       │
       ▼
Platform backend
       │ normalized events
       ▼
UI input router
       │ hit testing / focus / capture
       ▼
Control behavior
       │
       ▼
Application callback or binding update
       │
       ▼
Invalidation
```

---

## 7. Proposed package structure

### 7.1 Version 0.1 packages

Keep the initial package count small:

```text
Packages/
├── NGIN.UI/
│   ├── composition
│   ├── runtime
│   ├── layout
│   ├── controls
│   ├── input
│   ├── text contracts
│   ├── rendering contracts
│   └── platform contracts
│
├── NGIN.UI.Backend.SDL3/
│   ├── SDL3 platform integration
│   ├── SDL_GPU renderer
│   ├── surface management
│   ├── input translation
│   └── clipboard/cursor/IME integration
│
└── NGIN.UI.Hosting/
    └── optional NGIN.Core integration
```

`NGIN.UI.Backend.SDL3` may initially provide both the platform and renderer implementations. They remain separate interfaces internally even if one package implements both.

### 7.2 Possible later extractions

After the contracts stabilize, shared facilities may justify separate ecosystem packages:

```text
NGIN.Windowing
NGIN.Windowing.SDL3
NGIN.Windowing.Win32
NGIN.Graphics
NGIN.Graphics.SDLGPU
NGIN.Graphics.D3D12
NGIN.Graphics.Vulkan
NGIN.Graphics.Metal
NGIN.Text
NGIN.Accessibility
```

Do not create these broad abstractions before NGIN.UI has concrete requirements. A narrow backend API is less risky than designing an engine-sized graphics API upfront.

---

## 8. Immediate, retained, and declarative behavior

The words “immediate mode” and “retained mode” describe different aspects of a UI system and should not be treated as mutually exclusive implementation choices.

NGIN.UI should have:

### 8.1 Immediate composition

Application code reissues the current UI when a window is recomposed.

```cpp
void SettingsView(
    NGIN::UI::Composer& ui,
    SettingsModel& model)
{
    // Describe the UI that should exist now.
}
```

The application does not create and retain a `Button*`.

### 8.2 Retained runtime nodes

NGIN.UI retains a compact runtime node for each stable element identity.

A runtime node may contain:

```cpp
struct RuntimeNode
{
    ElementId id;
    ElementType type;

    LayoutState layout;
    InteractionState interaction;
    SemanticState semantics;

    StyleCache style;
    RenderCache render;

    RuntimeNode* parent;
    SmallVector<RuntimeNode*, 4> children;
};
```

The exact storage should be pool/index based rather than pointer-heavy, but this illustrates the responsibilities.

### 8.3 Declarative public syntax

The final ergonomic API may use small value descriptions:

```cpp
auto SettingsView(SettingsModel& model)
{
    using namespace NGIN::UI;

    return Column(
        Text("Account").Role(TextRole::Title),

        TextField(Bind(model.UserName))
            .Label("Username"),

        Row(
            Button("Cancel", [&] { model.Cancel(); }),

            Button("Save", [&] { model.Save(); })
                .Enabled(model.CanSave())
                .Variant(ButtonVariant::Primary)
        )
        .Align(HorizontalAlignment::End)
        .Gap(8_dp)
    )
    .Padding(24_dp)
    .Gap(16_dp);
}
```

That syntax should compile into the same composer operations as a lower-level scoped API.

The implementation should begin with an explicit `Composer` interface. The value-based frontend can be introduced after identity, lifecycle, and layout are proven.

---

## 9. Public API layers

Three API layers are recommended.

### 9.1 Backend contracts

Stable interfaces used by backend implementations:

```cpp
IPlatformBackend
IRenderBackend
ITextBackend
```

### 9.2 Composition API

Used by controls and advanced application code:

```cpp
Composer
CompositionContext
ElementScope
NodeProperties
```

### 9.3 Ergonomic view API

Used by most application code:

```cpp
Text(...)
Button(...)
TextField(...)
Column(...)
Row(...)
Grid(...)
ScrollView(...)
```

The ergonomic API must not be the only way to access the system. Custom controls should be able to compose semantic nodes and draw content without depending on private internals.

---

## 10. Application and window model

### 10.1 Standalone application

A plain native application should be possible:

```cpp
#include <NGIN/UI/UI.hpp>
#include <NGIN/UI/Backend/SDL3/SDL3.hpp>

int main()
{
    auto platform = NGIN::UI::SDL3::CreatePlatformBackend();
    auto renderer = NGIN::UI::SDL3::CreateRendererBackend();

    auto application = NGIN::UI::CreateApplication({
        .platform = std::move(platform),
        .renderer = std::move(renderer),
    });

    if (!application)
        return 1;

    MainViewModel model;

    auto window = application.Value()->CreateWindow({
        .id = "Main",
        .title = "NGIN.UI Gallery",
        .initialSize = {1280_dp, 720_dp},
        .minimumSize = {640_dp, 480_dp},
    });

    if (!window)
        return 2;

    window.Value()->SetContent(
        [&](NGIN::UI::Composer& ui)
        {
            ComposeMainView(ui, model);
        });

    return application.Value()->Run().HasValue() ? 0 : 3;
}
```

### 10.2 Window responsibilities

A UI window host owns:

- one backend native window;
- one render surface;
- one root runtime tree;
- one focus scope;
- one input queue;
- one frame scheduler;
- one accessibility root;
- window-level resources and theme;
- a content composition function.

### 10.3 Multiple windows

Multiple windows must be a first-class concept, even if the first milestone only uses one.

```cpp
auto inspector = app.Windows().Create({
    .id = "Inspector",
    .title = "Inspector",
});

inspector->SetContent(...);
```

Windows should not be children in the normal control tree. A window is a platform-owned top-level surface with a root UI tree.

### 10.4 Modal windows

A modal dialog is still a real window or platform modal surface. An in-window popup is a separate overlay concept.

Do not model every dialog as a platform window. NGIN.UI should distinguish:

- `Window`;
- `DialogWindow`;
- `Popup`;
- `Menu`;
- `Tooltip`;
- `Overlay`.

---

## 11. Platform backend contract

The platform backend should be narrow but complete enough for application UI.

```cpp
class IPlatformBackend
{
public:
    virtual ~IPlatformBackend() = default;

    virtual auto Initialize(PlatformInitInfo info)
        noexcept -> UIResult<void> = 0;

    virtual auto CreateWindow(const WindowCreateInfo& info)
        noexcept -> UIResult<PlatformWindowHandle> = 0;

    virtual auto DestroyWindow(PlatformWindowHandle window)
        noexcept -> UIResult<void> = 0;

    virtual auto ShowWindow(PlatformWindowHandle window)
        noexcept -> UIResult<void> = 0;

    virtual auto SetWindowTitle(
        PlatformWindowHandle window,
        NGIN::Text::StringView title)
        noexcept -> UIResult<void> = 0;

    virtual auto SetWindowBounds(
        PlatformWindowHandle window,
        PixelRect bounds)
        noexcept -> UIResult<void> = 0;

    virtual auto PollEvents(EventSink& sink)
        noexcept -> UIResult<void> = 0;

    virtual auto WaitEvents(
        EventSink& sink,
        std::chrono::milliseconds maximumWait)
        noexcept -> UIResult<void> = 0;

    virtual void WakeEventLoop() noexcept = 0;

    virtual auto SetCursor(
        PlatformWindowHandle window,
        CursorShape cursor)
        noexcept -> UIResult<void> = 0;

    virtual auto SetClipboardText(
        NGIN::Text::StringView text)
        noexcept -> UIResult<void> = 0;

    virtual auto GetClipboardText()
        noexcept -> UIResult<NGIN::Text::String> = 0;

    virtual auto StartTextInput(
        PlatformWindowHandle window,
        PixelRect candidateRect)
        noexcept -> UIResult<void> = 0;

    virtual auto StopTextInput(
        PlatformWindowHandle window)
        noexcept -> UIResult<void> = 0;

    virtual auto QueryDisplays()
        noexcept -> UIResult<DisplayList> = 0;
};
```

The backend translates native events into normalized NGIN.UI events. It does not decide which control receives them.

### 11.1 Normalized platform events

```cpp
using PlatformEvent = Variant<
    WindowCloseRequested,
    WindowResized,
    WindowMoved,
    WindowScaleChanged,
    WindowFocusChanged,
    PointerMoved,
    PointerButtonChanged,
    PointerWheelChanged,
    KeyChanged,
    TextInput,
    TextComposition,
    FileDrop,
    ThemeChanged
>;
```

Pointer events should distinguish:

```cpp
enum class PointerKind
{
    Mouse,
    Touch,
    Pen
};
```

Keyboard events and text input must be separate. A key press is not text.

---

## 12. Renderer backend contract

The renderer backend should consume UI-specific render data rather than expose a full Vulkan-like API.

```cpp
class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    virtual auto Initialize(RenderInitInfo info)
        noexcept -> UIResult<void> = 0;

    virtual auto CreateSurface(
        PlatformWindowHandle window,
        PixelSize initialSize)
        noexcept -> UIResult<RenderSurfaceHandle> = 0;

    virtual auto DestroySurface(
        RenderSurfaceHandle surface)
        noexcept -> UIResult<void> = 0;

    virtual auto ResizeSurface(
        RenderSurfaceHandle surface,
        PixelSize size)
        noexcept -> UIResult<void> = 0;

    virtual auto CreateTexture(
        const TextureCreateInfo& info)
        noexcept -> UIResult<TextureHandle> = 0;

    virtual auto UpdateTexture(
        TextureHandle texture,
        const TextureUpdateInfo& update)
        noexcept -> UIResult<void> = 0;

    virtual auto DestroyTexture(TextureHandle texture)
        noexcept -> UIResult<void> = 0;

    virtual auto Render(
        RenderSurfaceHandle surface,
        const RenderPacket& packet)
        noexcept -> UIResult<void> = 0;

    virtual auto Present(RenderSurfaceHandle surface)
        noexcept -> UIResult<void> = 0;

    virtual auto WaitIdle()
        noexcept -> UIResult<void> = 0;
};
```

The backend should not receive the runtime widget tree.

---

## 13. Rendering pipeline

A two-stage UI rendering representation is recommended.

### 13.1 High-level display list

Layout and controls generate device-independent drawing operations:

```cpp
using DisplayCommand = Variant<
    PushClipRect,
    PushClipRoundedRect,
    PopClip,
    PushTransform,
    PopTransform,
    FillRect,
    FillRoundedRect,
    StrokeRect,
    FillPath,
    StrokePath,
    DrawGlyphRun,
    DrawImage,
    BeginOpacityLayer,
    EndOpacityLayer
>;
```

This representation is useful for:

- debugging;
- snapshot tests;
- software rendering;
- renderer-independent caching;
- future PDF or remote output;
- accessibility bounds verification.

### 13.2 Low-level render packet

A UI renderer converts the display list into backend-consumable data:

```cpp
struct RenderPacket
{
    Span<const RenderVertex> vertices;
    Span<const NGIN::UInt32> indices;
    Span<const RenderBatch> batches;
    Span<const TextureUpdate> textureUpdates;
    PixelSize targetSize;
    float scaleFactor;
};
```

A batch may contain:

```cpp
struct RenderBatch
{
    TextureHandle texture;
    PixelRect scissor;
    NGIN::UInt32 firstIndex;
    NGIN::UInt32 indexCount;
    BlendMode blendMode;
};
```

This is close to the simplicity of Dear ImGui’s renderer contract while preserving a richer, testable display list above it.

### 13.3 Ownership boundary

`NGIN.UI` owns:

- element painting logic;
- display-list generation;
- primitive tessellation;
- batching;
- glyph atlas policy;
- clip stack;
- UI shaders and their portable source/precompiled forms.

The renderer backend owns:

- GPU device objects;
- swap chains or equivalent surfaces;
- command buffers;
- GPU buffers;
- texture allocation;
- shader-module creation;
- synchronization;
- presentation.

### 13.4 Initial rendering scope

Version 0.1 needs only:

- solid rectangles;
- rounded rectangles;
- borders;
- axis-aligned clipping;
- images;
- glyph runs;
- opacity;
- simple transforms.

Blur, shadows, arbitrary paths, gradients, and layers can follow.

---

## 14. Frame lifecycle

A frame for one window should proceed as follows:

```text
1. Platform backend receives events
2. Events are queued by window
3. Input router updates pointer/key/focus state
4. State or input marks the window dirty
5. Frame scheduler requests composition
6. View function reissues element declarations
7. Reconciler matches declarations to runtime nodes
8. Removed nodes receive cleanup
9. Style resolution runs for affected nodes
10. Measure pass runs
11. Arrange pass runs
12. Hit-test structure is updated
13. Semantic tree is updated
14. Paint pass generates display commands
15. UI renderer builds a render packet
16. Renderer backend submits and presents
17. Deferred callbacks/tasks are processed
```

Composition, layout, and painting should be independently invalidatable.

```cpp
enum class InvalidationKind : NGIN::UInt8
{
    None       = 0,
    Compose    = 1 << 0,
    Measure    = 1 << 1,
    Arrange    = 1 << 2,
    Paint      = 1 << 3,
    Semantics  = 1 << 4
};
```

Examples:

- changing text usually invalidates measure, arrange, paint, and semantics;
- changing background color invalidates paint;
- moving the pointer may only invalidate paint for hover state;
- changing a callback need not invalidate layout;
- resizing the window invalidates measure and arrange;
- scrolling may update transforms and paint without recomposing all children.

---

## 15. Event loop and idle behavior

A GUI event loop should wait when idle.

Conceptually:

```cpp
while (!application.ShouldExit())
{
    const auto nextDeadline =
        frameScheduler.NextAnimationDeadline();

    platform.WaitEvents(
        eventSink,
        nextDeadline - Clock::Now());

    application.ProcessEvents();

    for (auto& window : application.DirtyWindows())
        window.RenderFrame();
}
```

A frame should be requested when:

- a platform event arrives;
- state changes;
- a binding changes;
- an animation is active;
- a timer expires;
- a resource finishes loading;
- a window is exposed;
- a surface is resized;
- code explicitly calls `Invalidate`.

Continuous rendering should be opt-in for applications that need it.

---

## 16. Element identity and reconciliation

### 16.1 Default identity

Static children may be matched by:

- parent identity;
- element type;
- ordinal position.

This keeps simple views concise.

### 16.2 Explicit keys

Dynamic nodes require a stable key:

```cpp
ForEach(
    model.Documents,
    [](const Document& item) { return item.Id; },
    [&](Document& item)
    {
        return DocumentRow(item);
    });
```

Or:

```cpp
DocumentRow(item).Key(item.Id);
```

### 16.3 Identity rules

The proposal should define these rules early:

1. A node keeps runtime state while its identity and compatible type remain.
2. Changing a key destroys the old runtime node and creates a new one.
3. Reordering keyed siblings preserves their state.
4. Unkeyed dynamic sibling reordering has no preservation guarantee.
5. Local state belongs to the node identity.
6. Focus is restored only when the focused node remains present and focusable.

### 16.4 IDs are not labels

Visible labels must not double as identity. NGIN.UI should not require syntax such as `"Save##unique-id"`.

```cpp
Button("Save").Key("settings.save");
```

---

## 17. State and invalidation model

NGIN.UI should support three state categories.

### 17.1 Plain application state

```cpp
struct SettingsModel
{
    NGIN::Text::String userName;
    bool canSave;
};
```

Plain state is valid. The application explicitly invalidates after external changes, while input-triggered callbacks already occur inside a frame transaction.

### 17.2 Observable state

```cpp
UI::State<NGIN::Text::String> userName;
UI::State<bool> canSave;
```

`State<T>`:

- stores a value;
- exposes read/write access;
- notifies subscribers;
- schedules invalidation on its owning dispatcher;
- is not tied to a specific control.

### 17.3 Bindings

```cpp
TextField(Bind(model.UserName));
```

A binding provides typed access:

```cpp
template<typename T>
class Binding
{
public:
    auto Get() const -> const T&;
    auto Set(T value) const -> UIResult<void>;
    auto Subscribe(StateObserver<T> observer) -> Subscription;
};
```

A binding may wrap:

- `State<T>`;
- getter/setter functions;
- a model property;
- validation;
- conversion.

### 17.4 Computed values

```cpp
auto canSave = Computed(
    model.UserName,
    [](const auto& value)
    {
        return !value.IsEmpty();
    });
```

Computed values should be lazy and dependency-aware, but this can wait until after basic `State<T>` and `Binding<T>`.

### 17.5 Local component state

Local state is retained by element identity:

```cpp
auto expanded = ui.UseState<bool>(false);
```

Hooks should not be the first API implemented. A simpler typed local-state slot is sufficient initially.

The hook ordering contract must be documented if hooks are later introduced.

---

## 18. Component model

Most reusable UI should be ordinary functions.

```cpp
auto AccountHeader(const Account& account)
{
    return Row(
        Image(account.Avatar),
        Column(
            Text(account.DisplayName),
            Text(account.Email)
        )
    );
}
```

A stateful component abstraction is only needed when a unit owns:

- local state;
- lifecycle;
- resources;
- subscriptions;
- asynchronous work;
- imperative integration.

```cpp
class SearchBox final : public UI::Component
{
public:
    void Compose(UI::Composer& ui) override;

    void OnMounted(UI::ComponentContext&) override;
    void OnUnmounted(UI::ComponentContext&) override;

private:
    UI::State<NGIN::Text::String> m_query;
};
```

Avoid a large inheritance hierarchy where every `Text`, `Button`, and layout derives from a deeply virtual base exposed to users.

---

## 19. Layout model

Version 0.1 should implement a small constraint-based layout system.

### 19.1 Core geometry types

```cpp
struct Dp;
struct Px;
struct Percent;

struct Size;
struct Point;
struct Rect;
struct Thickness;
struct CornerRadius;
```

Use device-independent units for layout and pixels for render targets.

```cpp
using namespace NGIN::UI::Units;

Padding(16_dp);
Width(50_percent);
```

### 19.2 Measure and arrange

```cpp
class ILayoutBehavior
{
public:
    virtual auto Measure(
        LayoutContext& context,
        NodeHandle node,
        SizeConstraints constraints)
        noexcept -> Size = 0;

    virtual void Arrange(
        LayoutContext& context,
        NodeHandle node,
        Rect finalBounds)
        noexcept = 0;
};
```

### 19.3 Initial containers

Version 0.1:

- `Column`;
- `Row`;
- `Overlay`;
- `Padding`;
- `ScrollView`;
- `Spacer`.

Version 0.2:

- `Grid`;
- wrapping layout;
- absolute/canvas layout;
- virtualized list layout.

### 19.4 Constraints

```cpp
struct SizeConstraints
{
    Size minimum;
    Size maximum;
};
```

Use finite/infinite maximums rather than special magic sizes.

### 19.5 Alignment

```cpp
enum class HorizontalAlignment
{
    Start,
    Center,
    End,
    Stretch
};

enum class VerticalAlignment
{
    Start,
    Center,
    End,
    Stretch
};
```

### 19.6 Layout caching

Cache a node’s measured result using:

- constraints;
- style/layout revision;
- child layout revision;
- text measurement revision.

Do not optimize this until correctness tests exist, but design revisions into the node model.

---

## 20. Styling, themes, and resources

### 20.1 Typed style properties

Avoid a stringly typed CSS property map.

```cpp
struct ButtonStyle
{
    Brush background;
    Brush foreground;
    Border border;
    CornerRadius corners;
    Thickness padding;
    Typography typography;
    ControlStateStyles states;
};
```

### 20.2 Theme

```cpp
struct Theme
{
    ColorPalette colors;
    TypographyScale typography;
    SpacingScale spacing;
    ControlTheme controls;
    MotionTheme motion;
};
```

Themes should be immutable or revisioned data objects.

### 20.3 Resource scopes

A resource/environment scope may provide:

- theme;
- locale;
- text direction;
- scale factor;
- reduced-motion preference;
- high-contrast preference;
- default font collection;
- application services.

```cpp
ThemeScope(darkTheme,
    SettingsView(model));
```

### 20.4 Style resolution

Resolve styles in this order:

1. toolkit defaults;
2. active theme;
3. semantic variant;
4. inherited environment values;
5. element-specific modifiers;
6. interaction state, such as hover, press, focus, disabled.

### 20.5 No CSS parser initially

A future stylesheet frontend may compile into typed style objects. The runtime should not begin with a parser or arbitrary string selectors.

---

## 21. Input routing

NGIN.UI should implement:

- hit testing;
- tunneling/capture phase;
- target phase;
- bubbling phase;
- pointer capture;
- keyboard focus;
- focus traversal;
- command routing;
- gesture recognition later.

```cpp
enum class EventPhase
{
    Capture,
    Target,
    Bubble
};
```

Controls should be able to mark an event handled.

### 21.1 Pointer capture

Pointer capture is required for:

- button presses;
- dragging;
- sliders;
- scrollbars;
- text selection;
- window-independent drag behavior.

### 21.2 Keyboard focus

Focus requires:

- one focused element per focus scope;
- tab order;
- directional navigation;
- programmatic focus requests;
- focus-visible styling;
- restoration when popups close;
- transfer on node removal.

### 21.3 Commands

Controls may expose callbacks directly:

```cpp
Button("Save", [&] { model.Save(); });
```

A reusable command abstraction can be added:

```cpp
Command<void()> saveCommand;
Command<Document&(DocumentId)> openDocument;
```

Do not require `ICommand`-style boxing for every event.

---

## 22. Text system

Text is a foundational subsystem, not a late rendering detail.

### 22.1 Requirements

The text stack must eventually handle:

- Unicode;
- script detection;
- bidirectional text;
- shaping;
- font fallback;
- line breaking;
- wrapping;
- alignment;
- selection geometry;
- cursor movement by grapheme cluster;
- IME composition;
- variable fonts;
- color emoji;
- DPI scaling;
- accessibility text ranges.

### 22.2 Recommended initial stack

Use:

- HarfBuzz for shaping;
- FreeType for loading and rasterizing fonts;
- a NGIN.UI glyph cache/atlas;
- platform APIs only for font discovery where necessary.

Do not use a simple “one codepoint equals one glyph” renderer for the core architecture.

### 22.3 Text interfaces

```cpp
class IFontProvider
{
public:
    virtual auto ResolveFont(const FontRequest&)
        noexcept -> UIResult<FontFaceHandle> = 0;
};

class ITextShaper
{
public:
    virtual auto Shape(const TextRun&, FontFaceHandle)
        noexcept -> UIResult<ShapedRun> = 0;
};

class ITextLayout
{
public:
    virtual auto LayoutParagraph(const ParagraphRequest&)
        noexcept -> UIResult<ParagraphLayout> = 0;
};
```

### 22.4 Version 0.1 text scope

The first vertical slice may support:

- UTF-8 input;
- one bundled font;
- left-to-right single-line shaping;
- glyph atlas rendering;
- basic measurement.

Before calling version 0.1 “general purpose,” add:

- font fallback;
- multiline layout;
- selection/cursor;
- IME;
- bidirectional text.

---

## 23. Text editing

`TextField` should separate:

1. application value;
2. editing session state;
3. validation;
4. visual presentation.

Editing state contains:

```cpp
struct TextEditingState
{
    TextRange selection;
    TextRange composition;
    NGIN::UIntSize caretCluster;
    float preferredCaretX;
    bool revealPassword;
};
```

A text field should not update application state with invalid intermediate UTF-8.

Recommended editing transaction:

```text
native text/composition event
        ↓
editing buffer
        ↓
grapheme-aware operation
        ↓
validation/filter
        ↓
binding update
        ↓
recomposition and paint
```

Password fields must not expose their content through ordinary semantic text or logs.

---

## 24. Semantics and accessibility

Every semantic control should produce an accessibility node.

```cpp
struct SemanticNode
{
    SemanticRole role;
    NGIN::Text::String label;
    NGIN::Text::String value;
    NGIN::Text::String description;

    Rect bounds;
    SemanticStateFlags states;
    SemanticActionFlags actions;

    ElementId owner;
    SmallVector<SemanticNodeId, 4> children;
};
```

Roles include:

- window;
- group;
- heading;
- text;
- button;
- checkbox;
- textbox;
- list;
- list item;
- image;
- link;
- slider.

The semantic tree should be distinct from the render tree. Decorative nodes should not appear in accessibility.

Platform bridges can later map the semantic tree to:

- UI Automation on Windows;
- NSAccessibility on macOS;
- AT-SPI on Linux;
- mobile equivalents.

The core semantic model must exist before those bridges, otherwise accessibility becomes a rewrite.

---

## 25. Images and resources

Images should be represented by logical resources, not raw backend texture pointers.

```cpp
class ImageSource;
class ImageHandle;
class ImageCache;
```

Possible sources:

- memory;
- file;
- module resource;
- generated pixels;
- SVG/vector source later;
- native external texture escape hatch.

Resource lifecycle:

```text
ImageSource
   ↓ asynchronous decode
CPU image
   ↓ upload request
TextureHandle
   ↓ cached by renderer generation
DrawImage command
```

If a renderer device is recreated, logical image resources must survive and be re-uploadable.

---

## 26. Native and custom escape hatches

### 26.1 Custom painting

```cpp
Canvas(
    [](PaintContext& context, Rect bounds)
    {
        context.FillRoundedRect(...);
        context.DrawGlyphRun(...);
    });
```

Custom painting participates in layout, clipping, transforms, hit testing, and semantics.

### 26.2 Native view

A future native view host may embed:

- browser controls;
- video players;
- platform text editors;
- map controls;
- third-party native SDK views.

```cpp
NativeView(
    NativeViewDescriptor{
        .factory = ...,
        .update = ...,
        .destroy = ...
    });
```

Native views are an escape hatch and should not define the default control model.

---

## 27. Animation

Animation should be based on invalidation deadlines.

```cpp
auto opacity = Animate(
    targetOpacity,
    AnimationSpec{
        .timing = TweenTiming{
            .duration = 180ms,
            .curve = EasingCurve::Standard(),
        },
    });
```

The scheduler requests frames only while animations are active.

Version 0.1 only needs:

- scalar interpolation;
- color interpolation;
- transform interpolation;
- opacity;
- reduced-motion support.

Layout transitions and shared-element transitions can follow later.

---

## 28. Threading and asynchronous work

### 28.1 UI thread

Each application has one UI dispatcher, normally bound to the platform main thread.

These operations must occur on the UI thread:

- native window creation/destruction;
- event processing;
- composition;
- runtime tree mutation;
- focus changes;
- most renderer surface operations.

### 28.2 Worker tasks

Use workers for:

- file I/O;
- image decoding;
- expensive data transforms;
- font preprocessing;
- application work.

Completion posts back to the UI dispatcher.

### 28.3 Render thread

Do not require a separate render thread in version 0.1.

The architecture may later support:

```text
UI thread builds immutable RenderPacket N
Render thread submits packet N
UI thread builds packet N+1
```

This requires explicit resource generations, immutable packets, and synchronized surface resize. It should not complicate the first implementation.

### 28.4 Async callbacks

```cpp
Button("Save", [&]() -> UI::Task<void>
{
    co_await model.SaveAsync();
});
```

A component-scoped cancellation source should cancel work when the owning node is removed, unless the task explicitly detaches.

---

## 29. NGIN.Core integration

### 29.1 Current issue

A generic host loop that repeatedly calls `Tick()` and sleeps is not ideal for GUI applications. Desktop GUI platforms need their event dispatch to own or coordinate the main thread and should sleep through their platform event wait function.

### 29.2 Proposed generic Core extension

Add a generic run-loop interface to `NGIN.Core`, not a UI-specific dependency.

```cpp
class IHostRunLoop
{
public:
    virtual ~IHostRunLoop() = default;

    virtual auto Run(IApplicationHost& host)
        noexcept -> CoreResult<void> = 0;

    virtual void Wake() noexcept = 0;
};
```

`ApplicationBuilder` should support:

```cpp
builder->UseRunLoop(runLoop);
```

The default implementation preserves the existing tick loop.

`NGIN.UI.Hosting` supplies a platform-event-driven run loop.

### 29.3 Hosted module

```cpp
class UIModule final : public NGIN::Core::IModule
{
public:
    auto OnRegister(NGIN::Core::ModuleContext&)
        noexcept -> NGIN::Core::CoreResult<void> override;

    auto OnInit(NGIN::Core::ModuleContext&)
        noexcept -> NGIN::Core::CoreResult<void> override;

    auto OnStart(NGIN::Core::ModuleContext&)
        noexcept -> NGIN::Core::CoreResult<void> override;

    auto OnStop(NGIN::Core::ModuleContext&)
        noexcept -> NGIN::Core::CoreResult<void> override;
};
```

Services:

```text
NGIN.UI.IApplication
NGIN.UI.IWindowManager
NGIN.UI.IUIDispatcher
NGIN.UI.IThemeManager
NGIN.UI.IPlatformBackend
NGIN.UI.IRenderBackend
NGIN.UI.IResourceLoader
```

### 29.4 Module stages and capabilities

Suggested modules:

```text
NGIN.UI.Platform.SDL3
    Family: Platform
    Stage: Platform
    Capability: UI.PlatformBackend (exclusive)

NGIN.UI.Renderer.SDLGPU
    Family: Platform
    Stage: Platform
    Capability: UI.RenderBackend (exclusive)

NGIN.UI.Runtime
    Family: Core or Platform
    Stage: Presentation
    Requires:
        UI.PlatformBackend
        UI.RenderBackend

MyApp.Presentation
    Family: App
    Stage: Presentation
```

The exact module family should be checked against NGIN.Core’s dependency-layer rules before implementation. The backend modules clearly belong to `Platform`; the UI runtime may be a normal linked package plus a presentation-stage module.

### 29.5 Standalone first

Implement the standalone UI loop first. Add hosted integration only after the first backend displays and interacts with a real window. This keeps UI bugs separate from host-loop changes.

---

## 30. Error handling and diagnostics

Use a UI-specific result type consistent with NGIN conventions.

```cpp
enum class UIErrorCode
{
    InvalidArgument,
    InvalidState,
    WrongThread,
    BackendUnavailable,
    WindowCreationFailed,
    SurfaceCreationFailed,
    RenderFailed,
    ResourceFailed,
    TextShapingFailed,
    Unsupported,
    OutOfMemory
};

using UIResult<T> =
    NGIN::Utilities::Expected<T, UIError>;
```

Backend errors should preserve:

- backend name;
- operation;
- logical resource or window ID;
- native error code;
- readable message.

Diagnostics should include:

- frame timings;
- composition count;
- reconciled/created/removed node counts;
- measured and arranged node counts;
- display command count;
- draw batch count;
- vertices and indices;
- texture uploads;
- glyph-cache hits/misses;
- invalidation causes;
- active windows;
- focused element;
- pointer capture owner.

A built-in inspector should be planned early, even if it is not in the first milestone.

---

## 31. Testing strategy

### 31.1 Headless platform backend

```cpp
TestPlatformBackend
```

It should:

- create logical windows without OS windows;
- inject input events;
- record cursor and clipboard requests;
- simulate DPI changes and resize;
- provide deterministic time.

### 31.2 Recording renderer

```cpp
RecordingRenderBackend
```

It records:

- surface operations;
- texture updates;
- render packets;
- present calls.

### 31.3 Test categories

#### Composition tests

- node creation;
- keyed preservation;
- branch replacement;
- component mount/unmount;
- local state preservation.

#### Layout tests

- constraints;
- padding;
- row/column distribution;
- alignment;
- scroll viewport;
- DPI conversion.

#### Input tests

- hit testing;
- bubbling;
- pointer capture;
- focus traversal;
- button click;
- text input.

#### Display-list tests

- expected commands;
- clip nesting;
- transforms;
- text placement;
- opacity.

#### Snapshot tests

Render deterministic images with a software/reference renderer and compare with tolerances.

#### Backend smoke tests

- create/show/close window;
- resize surface;
- keyboard/mouse input;
- DPI change;
- clipboard;
- IME activation;
- device loss where applicable.

### 31.4 No screenshots as the only correctness test

Most behavior should be tested structurally. Pixel snapshots should complement, not replace, semantic and layout assertions.

---

## 32. Performance and memory strategy

### 32.1 Runtime storage

Use:

- generational handles;
- packed node arrays or pools;
- small vectors for children;
- frame arenas for temporary declarations and display lists;
- revision counters;
- cached text/layout results.

Avoid:

- one heap allocation per modifier;
- `std::function` for every small callback if NGIN has or can provide an SBO callback;
- shared ownership between every node;
- virtual calls for every trivial style property;
- rebuilding GPU pipelines per frame.

### 32.2 Callback storage

A callback wrapper should support:

- small-buffer optimization;
- move-only captures;
- explicit lifetime;
- optional weak component ownership.

```cpp
UI::Callback<void(const ClickEvent&)>
```

### 32.3 Dirty-region rendering

Do not require dirty-region rendering in version 0.1. Full-window repaint is simpler and usually acceptable with GPU rendering.

The architecture should still track paint invalidation by node. Later, this can support:

- cached layers;
- partial display-list rebuilding;
- partial surface damage.

### 32.4 Virtualization

Do not implement list virtualization until basic scrolling is stable. Ensure the layout and identity model can later support a virtualizing container that materializes only visible children.

---

## 33. ABI and versioning

### 33.1 C++ API

NGIN.UI may initially prioritize source compatibility over a stable binary ABI.

### 33.2 Backend ABI

Backend interfaces cross package boundaries. To reduce fragility:

- keep interfaces narrow;
- pass opaque handles;
- avoid standard-library containers in plugin ABI boundaries if dynamic third-party backends are planned;
- version backend contracts;
- expose capability flags;
- validate backend versions at startup.

### 33.3 Capability negotiation

```cpp
enum class PlatformCapability
{
    Clipboard,
    IME,
    MultipleWindows,
    FileDrop,
    PenInput,
    TouchInput,
    NativeDialogs
};

enum class RenderCapability
{
    TextureUpdates,
    ScissorRects,
    Index32,
    OffscreenTargets,
    DeviceLossRecovery
};
```

Core controls should have graceful fallbacks where possible.

---

## 34. Suggested repository layout

```text
Packages/NGIN.UI/
├── CMakeLists.txt
├── NGIN.UI.nginpkg
├── README.md
├── include/NGIN/UI/
│   ├── UI.hpp
│   ├── Application.hpp
│   ├── Composer.hpp
│   ├── Element.hpp
│   ├── State.hpp
│   ├── Binding.hpp
│   ├── Geometry.hpp
│   ├── Style.hpp
│   ├── Theme.hpp
│   ├── Events.hpp
│   ├── Semantics.hpp
│   ├── Text.hpp
│   ├── Rendering.hpp
│   ├── Platform.hpp
│   ├── Controls/
│   │   ├── Text.hpp
│   │   ├── Button.hpp
│   │   ├── TextField.hpp
│   │   └── ScrollView.hpp
│   └── Layout/
│       ├── Row.hpp
│       ├── Column.hpp
│       ├── Overlay.hpp
│       └── Constraints.hpp
├── src/NGIN/UI/
│   ├── Application.cpp
│   ├── Composer.cpp
│   ├── RuntimeTree.cpp
│   ├── Reconciler.cpp
│   ├── Layout.cpp
│   ├── Input.cpp
│   ├── Focus.cpp
│   ├── Semantics.cpp
│   ├── DisplayList.cpp
│   ├── UIRenderer.cpp
│   └── Controls/
├── tests/
└── examples/
    └── Gallery/
```

Backend:

```text
Packages/NGIN.UI.Backend.SDL3/
├── CMakeLists.txt
├── NGIN.UI.Backend.SDL3.nginpkg
├── include/NGIN/UI/Backend/SDL3/
└── src/NGIN/UI/Backend/SDL3/
    ├── SDLPlatformBackend.cpp
    ├── SDLWindow.cpp
    ├── SDLInput.cpp
    ├── SDLGpuRenderer.cpp
    ├── SDLSurface.cpp
    └── SDLClipboard.cpp
```

Hosting:

```text
Packages/NGIN.UI.Hosting/
├── CMakeLists.txt
├── NGIN.UI.Hosting.nginpkg
├── include/NGIN/UI/Hosting/
└── src/NGIN/UI/Hosting/
    ├── UIModule.cpp
    ├── UIHostRunLoop.cpp
    └── BuilderExtensions.cpp
```

---

## 35. Package manifest sketch

Core UI package:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4"
         Name="NGIN.UI"
         Version="0.1.0"
         CompatiblePlatformRange=">=0.1.0 &lt;0.2.0">
  <Build Backend="CMake" Mode="AddSubdirectory">
    <Options>
      <Option Name="NGIN_UI_BUILD_TESTS" Value="OFF" />
      <Option Name="NGIN_UI_BUILD_EXAMPLES" Value="OFF" />
    </Options>
  </Build>

  <Uses>
    <Package Name="NGIN.Base"
             Version=">=0.1.0 &lt;0.2.0"
             Scope="Target" />
  </Uses>

  <Library Name="NGIN.UI">
    <Exports>
      <LibraryTarget Name="NGIN::UI"
                     Linkage="Static" />
    </Exports>
  </Library>

  <Compatibility>
    <OperatingSystems>
      <OperatingSystem Name="windows" />
      <OperatingSystem Name="linux" />
      <OperatingSystem Name="macos" />
    </OperatingSystems>
  </Compatibility>
</Package>
```

SDL3 backend package:

```xml
<Package SchemaVersion="4"
         Name="NGIN.UI.Backend.SDL3"
         Version="0.1.0"
         CompatiblePlatformRange=">=0.1.0 &lt;0.2.0">
  <Uses>
    <Package Name="NGIN.UI"
             Version=">=0.1.0 &lt;0.2.0"
             Scope="Target" />
    <Package Name="SDL3"
             Version=">=3.2.0"
             Scope="Target" />
  </Uses>

  <Library Name="NGIN.UI.Backend.SDL3">
    <Exports>
      <LibraryTarget Name="NGIN::UI::Backend::SDL3"
                     Linkage="Static" />
    </Exports>
  </Library>
</Package>
```

The exact third-party package declaration depends on NGIN’s package-provider model.

---

## 36. First vertical slice

The first implementation should prove the entire architecture with the smallest useful UI.

### 36.1 Scope

One native window containing:

```text
Column
├── Text: "NGIN.UI"
├── Text: counter value
├── Row
│   ├── Button: "-"
│   └── Button: "+"
└── TextField
```

Required behavior:

- native window creation;
- event loop;
- surface creation;
- clear and present;
- text shaping and glyph rendering;
- row/column layout;
- pointer hit testing;
- button hover/press/click;
- focus;
- keyboard input;
- text field editing;
- state update;
- invalidation;
- window resize;
- clean shutdown;
- headless tests of composition and layout.

### 36.2 Explicitly excluded from the first slice

- themes beyond one hardcoded default;
- accessibility bridge;
- multiple windows;
- IME;
- scroll view;
- async image loading;
- animation;
- Core hosting;
- markup;
- native views.

### 36.3 Why this slice

It crosses every critical boundary:

```text
model
→ composition
→ runtime identity
→ layout
→ input
→ text
→ display list
→ renderer
→ native window
```

A prototype that only draws rectangles does not validate text, input, focus, or state, which are the hard parts of application UI.

---

## 37. Milestone plan

### Milestone 0 — Contracts and test harness

Deliver:

- geometry and unit types;
- IDs and handles;
- error types;
- platform event types;
- platform backend interface;
- renderer backend interface;
- headless platform backend;
- recording renderer;
- architecture tests.

Exit criterion:

A logical window can be created headlessly, receive injected resize/input events, and record an empty presented frame.

### Milestone 1 — Native window and renderer

Deliver:

- SDL3 platform initialization;
- SDL window;
- SDL event translation;
- SDL_GPU device and surface;
- one solid rectangle;
- resize and shutdown.

Exit criterion:

A resizable window displays a stable colored surface on Windows.

### Milestone 2 — Composition and runtime tree

Deliver:

- composer;
- element declarations;
- runtime node pool;
- static reconciliation;
- keyed child reconciliation;
- mount/unmount lifecycle;
- composition invalidation.

Exit criterion:

Conditional and keyed test trees preserve or destroy runtime state according to documented identity rules.

### Milestone 3 — Layout

Deliver:

- measure/arrange;
- row;
- column;
- padding;
- overlay;
- alignment;
- layout debugging overlay.

Exit criterion:

Layout tests cover finite, infinite, minimum, maximum, and DPI-scaled constraints.

### Milestone 4 — Text

Deliver:

- bundled font;
- HarfBuzz shaping;
- FreeType rasterization;
- glyph atlas;
- `Text`;
- measurement;
- clipping.

Exit criterion:

The gallery displays correctly shaped UTF-8 text and reflows on resize.

### Milestone 5 — Input and button

Deliver:

- hit testing;
- pointer routing;
- hover;
- press;
- pointer capture;
- focus basics;
- `Button`.

Exit criterion:

Button interactions are deterministic in headless tests and work in the SDL window.

### Milestone 6 — State and text field

Deliver:

- `State<T>`;
- `Binding<T>`;
- keyboard routing;
- text input;
- caret;
- selection;
- `TextField`;
- clipboard basics.

Exit criterion:

Typing updates bound state and causes only the required invalidation.

### Milestone 7 — Application foundations

Deliver:

- theme/resource scope;
- scroll view;
- multiple windows;
- dialogs/popups foundation;
- semantic tree;
- diagnostics inspector.

Exit criterion:

A small settings-style desktop app can be built without private API access.

### Milestone 8 — NGIN.Core hosting

Deliver:

- generic Core run-loop extension;
- UI hosting module;
- service registration;
- task/dispatcher bridge;
- hosted gallery example.

Exit criterion:

The same gallery runs standalone and hosted with the UI package unchanged.

---

## 38. Implementation order inside the core

Recommended class order:

1. `UIError`, `UIResult`;
2. geometry and units;
3. opaque/generational handles;
4. platform events;
5. backend contracts;
6. test platform and recording renderer;
7. application/window lifecycle;
8. runtime node pool;
9. composer;
10. reconciler;
11. layout;
12. display list;
13. simple rectangle renderer;
14. text;
15. hit testing;
16. focus;
17. button;
18. state/binding;
19. text field;
20. resource/theme scopes;
21. semantics;
22. hosting.

Do not begin by designing dozens of controls.

---

## 39. API sketch for the gallery

This is a target-level sketch, not a frozen API:

```cpp
#include <NGIN/UI/UI.hpp>

namespace Gallery
{
    using namespace NGIN::UI;
    using namespace NGIN::UI::Units;

    struct MainModel
    {
        State<int> counter{0};
        State<NGIN::Text::String> name{};
    };

    auto MainView(MainModel& model)
    {
        return Column(
            Text("NGIN.UI")
                .Role(TextRole::Title),

            Text(Format("Counter: {}", model.counter.Get())),

            Row(
                Button("-", [&]
                {
                    model.counter.Set(model.counter.Get() - 1);
                }),

                Button("+", [&]
                {
                    model.counter.Set(model.counter.Get() + 1);
                })
                .Variant(ButtonVariant::Primary)
            )
            .Gap(8_dp),

            TextField(Bind(model.name))
                .Label("Name")
                .Placeholder("Enter your name"),

            Text(Computed([&]
            {
                return model.name.Get().IsEmpty()
                    ? NGIN::Text::String{"Hello"}
                    : Format("Hello, {}", model.name.Get());
            }))
        )
        .Padding(24_dp)
        .Gap(16_dp)
        .MaximumWidth(640_dp);
    }
}
```

Window setup:

```cpp
int main()
{
    auto application =
        NGIN::UI::CreateApplication(
            NGIN::UI::SDL3::CreateBackend());

    if (!application)
        return 1;

    Gallery::MainModel model;

    auto window = application.Value()->CreateWindow({
        .id = "Gallery.Main",
        .title = "NGIN.UI Gallery",
        .initialSize = {900_dp, 640_dp},
    });

    if (!window)
        return 2;

    window.Value()->SetContent(
        [&]
        {
            return Gallery::MainView(model);
        });

    return application.Value()->Run()
        ? 0
        : 3;
}
```

The first internal prototype may use a less polished `Composer` API. The public syntax should only be frozen after the vertical slice exposes the real requirements.

---

## 40. Decision log

### Decision 1: Do not put concrete platform code in `NGIN.UI`

**Accepted.**

Concrete Win32, SDL, Cocoa, Wayland, Vulkan, D3D, and Metal code belongs in backend packages.

### Decision 2: Keep backend contracts in or adjacent to NGIN.UI initially

**Accepted.**

A narrow UI backend contract should be implemented before inventing broad `NGIN.Windowing` and `NGIN.Graphics` ecosystems.

### Decision 3: Use Dear ImGui-style backend separation

**Accepted.**

Use separate platform and renderer responsibilities, even when one package implements both.

### Decision 4: Do not use pure Dear ImGui widget semantics

**Accepted.**

NGIN.UI requires automatic layout, retained interaction state, accessibility semantics, event-driven frames, and application controls.

### Decision 5: Use a hybrid composition model

**Accepted.**

Application code reissues UI descriptions. NGIN.UI retains runtime nodes and state.

### Decision 6: Use custom-rendered controls by default

**Accepted.**

This provides consistent behavior and styling. Native embedding remains an escape hatch.

### Decision 7: Use a high-level display list and low-level render packet

**Accepted.**

This preserves testability while keeping renderer backends small.

### Decision 8: SDL3 + SDL_GPU is the first backend

**Accepted for the prototype.**

It minimizes initial platform and graphics work and provides a path across Windows, Linux, and macOS.

### Decision 9: Standalone runtime before `NGIN.Core` integration

**Accepted.**

Core integration follows after a real UI window works.

### Decision 10: Text architecture is designed early

**Accepted.**

HarfBuzz/FreeType integration begins before complex controls.

### Decision 11: No markup language initially

**Accepted.**

A markup generator may be added only after the typed C++ API and runtime model stabilize.

---

## 41. Open design questions

These do not block initial contract work, but should be resolved before freezing the public API.

1. Should the public frontend primarily return typed view values or compose directly into an explicit `Composer&`?
2. Should `State<T>` be part of `NGIN.UI` or a reusable `NGIN.Reactive` package later?
3. Should the UI renderer tessellate every primitive, or should some backends consume high-level display commands?
4. What callback abstraction from `NGIN.Base` should be used instead of `std::function`?
5. Which NGIN string type is canonical at public boundaries?
6. Should window IDs be user strings, hashes, or dedicated strongly typed values?
7. How should bundled resources be addressed relative to module roots?
8. Is the generic Core run-loop extension accepted as a Core responsibility?
9. How should shader binaries for SDL_GPU be generated and staged by NGIN?
10. Should version 0.1 support dynamic linking, or only static library targets?
11. Which license and package mechanism will be used for SDL3, HarfBuzz, and FreeType?
12. How much bidirectional and fallback text support is required before the first public release?

---

## 42. Immediate next actions

The next implementation work should be:

1. Add this proposal under `docs/proposals/`.
2. Create `Packages/NGIN.UI/` with only contracts and tests.
3. Define `UIError`, geometry, units, handles, and events.
4. Define `IPlatformBackend` and `IRenderBackend`.
5. Implement `TestPlatformBackend` and `RecordingRenderBackend`.
6. Create `Packages/NGIN.UI.Backend.SDL3/`.
7. Open a native SDL3 window and clear/present through SDL_GPU.
8. Add the runtime node pool and explicit composer.
9. Implement `Column`, `Row`, and a rectangle node.
10. Add HarfBuzz/FreeType text.
11. Add hit testing and `Button`.
12. Add `State<T>`, binding, and `TextField`.
13. Only then freeze the ergonomic declarative API.
14. Add `NGIN.UI.Hosting` after the standalone gallery works.

---

## 43. Definition of architectural success

The architecture is validated when all of the following are true:

- `NGIN.UI` compiles without SDL, Win32, Vulkan, D3D, or Metal headers.
- A test backend can run composition, layout, input, and display-list tests without opening a window.
- The SDL3 backend can be replaced without modifying controls.
- The renderer can be replaced without modifying window/input code.
- Application code owns its domain state and does not own native widgets.
- Focus, editing, and scroll state survive recomposition through stable identity.
- An idle application waits rather than continuously polling.
- The same root view can run in standalone and `NGIN.Core`-hosted applications.
- Text is shaped rather than rendered as independent codepoints.
- Semantic information exists independently of pixels.
- The first gallery uses only public APIs.
- Adding a new control does not require editing platform backends.
- Adding a new renderer does not require editing the reconciler or layout engine.

---

## 44. Final recommendation

Start with the architecture of Dear ImGui, not a clone of Dear ImGui.

Copy:

- backend-neutral UI core;
- separate platform and renderer backends;
- application-reissued UI declarations;
- simple renderer handoff;
- application-owned state.

Add what a general-purpose application framework needs:

- retained runtime identity;
- automatic layout;
- event-driven invalidation;
- semantic controls;
- typed state and bindings;
- focus and text editing;
- accessibility;
- multiple native windows;
- resources and themes;
- testable display lists;
- optional NGIN.Core hosting.

Avoid building a complete windowing or graphics ecosystem before the UI has proven requirements. Define narrow contracts, implement the first SDL3 backend, and extract broader NGIN packages only when the interfaces have survived real controls, text, input, resizing, and multiple windows.
