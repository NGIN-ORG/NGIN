# NGIN.Platform Implementation Plan

Status: Proposed

Initial operating systems: Windows and Linux

Target language: C++23

Primary initial consumer: `NGIN.UI`

Related plans:

- [`NGIN.UI-Implementation-Plan.md`](NGIN.UI-Implementation-Plan.md)
- [`NGIN.UI-V0.2-Roadmap.md`](NGIN.UI-V0.2-Roadmap.md)
- [`NGIN-V4-North-Star-And-Subsystem-Plans.md`](NGIN-V4-North-Star-And-Subsystem-Plans.md)

## Summary

`NGIN.Platform` should become NGIN's native desktop windowing and input
boundary. It should provide one portable public contract, direct native
implementations for Windows and Linux, deterministic test support, and the
native surface information required by independently selected renderers.

The initial native implementation should use:

- Win32 on Windows;
- Wayland as the preferred Linux backend;
- X11 as the Linux fallback.

Linux backend selection should occur at runtime. Applications should not need
different source code or package selections for Wayland and X11.

`NGIN.Platform` does not render controls, own application file I/O, or define
NGIN.UI behavior. `NGIN.UI` remains custom-rendered and consumes normalized
platform events. File management remains in `NGIN.Base`.

SDL3 remains available only as an explicit migration provider while the native
platform and renderer paths are brought online. It is not a permanent fallback
inside `NGIN.Platform`.

## Goals

- Replace the windowing and input responsibilities currently implemented by
  `NGIN.UI.Backend.SDL3`.
- Provide native Windows and Linux support in the first public release.
- Keep NGIN.UI controls, composition, layout, input routing, and semantics
  independent of operating-system APIs.
- Keep platform and renderer selection independent.
- Normalize window, input, text, display, clipboard, cursor, and system
  preference behavior without pretending unsupported operations are portable.
- Support event-driven applications that wait while idle and can be woken from
  another thread.
- Preserve deterministic headless testing.
- Expose explicit contract versions, capabilities, diagnostics, and structured
  failures.
- Make the first contract useful outside NGIN.UI without turning the package
  into a general operating-system utility collection.

## Non-Goals For Version 0.1

- File reading, writing, watching, paths, or directory management.
- Rendering, graphics devices, shaders, or presentation policy.
- Audio.
- Native UI controls.
- Native file and folder dialogs.
- Gamepad support.
- Complete touch and pen support.
- Mobile, macOS, browser, or console backends.
- Process management, dynamic-library loading, networking, or timers already
  owned by another NGIN foundation area.

Gamepads, touch, pen, and native dialogs may be represented by reserved or
future capability concepts, but a backend must not advertise behavior it does
not implement.

## Product Boundary

The intended dependency direction is:

```text
NGIN.Base
    ↑
NGIN.Platform
    ↑
NGIN.UI
    ↑
NGIN.UI.Hosting
```

Rendering remains a sibling integration selected by the application:

```text
NGIN.Platform ── native surface source ──→ NGIN.UI renderer provider
      ↑                                         ↑
      └──────────────── NGIN.UI ────────────────┘
```

The rules are:

- `NGIN.Platform` must not depend on `NGIN.UI`.
- `NGIN.UI` may depend on the portable `NGIN.Platform` contract.
- Native backend source files may include operating-system headers; portable
  public headers must not require them.
- Renderer providers may consume tagged native surface information without
  owning the platform event loop.
- Adding a platform backend must not require changes to UI controls, layout,
  reconciliation, or display-list construction.
- Adding a renderer must not require changes to window or input behavior.

## Repository Ownership

The recommended authored layout is:

```text
Dependencies/NGIN/NGIN.Platform/
├── include/NGIN/Platform/
├── src/Common/
├── src/Windows/
├── src/Linux/Common/
├── src/Linux/Wayland/
├── src/Linux/X11/
└── tests/

Packages/NGIN.Platform/
├── NGIN.Platform.nginpkg
└── CMakeLists.txt

Packages/NGIN.Platform.Backend.SDL3/
└── explicit temporary migration provider
```

The package exports:

```text
Package:   NGIN.Platform
Target:    NGIN::Platform
Namespace: NGIN::Platform
```

`Dependencies/NGIN/NGIN.Platform` owns the reusable first-party source.
`Packages/NGIN.Platform` owns workspace exposure and package metadata.

## Public Programming Model

The normal application path should create one explicitly owned platform
context:

```cpp
auto platform = NGIN::Platform::Create({
    .applicationName = "My Application",
    .backend = NGIN::Platform::BackendPreference::Automatic,
});
```

`Automatic` means:

- Win32 on Windows;
- Wayland on Linux when a usable Wayland connection exists;
- X11 on Linux when Wayland is unavailable;
- a structured error when no supported display backend can initialize.

The implementation should not use an implicit process-global singleton. Native
APIs with process-global constraints remain encapsulated by the owned context.

The primary public concepts should be:

```text
PlatformContext
Window
WindowId
PlatformEvent
PlatformCapabilities
TextInputSession
ClipboardRequestId
SurfaceSource
PlatformDiagnostics
```

`Window` should be move-only and owned through RAII. Events should identify
windows through stable generational `WindowId` values instead of native
pointers. The context must outlive its windows.

The public facade may own an internal backend interface, but applications
should operate on the facade rather than manually dispatching through backend
v-tables.

## Contract Rules

### Errors And Results

`NGIN.Platform` should define a platform-owned structured error boundary rather
than depend on `UIError`.

Errors should include:

- a stable platform error code;
- a portable message;
- provider name;
- failed operation;
- optional native error code;
- optional window or request identity.

No exception should cross the public platform boundary. Allocation failure and
unexpected provider exceptions must be normalized into platform results.

### Contract Versioning And Capabilities

Providers publish:

- a contract version;
- provider identity;
- operating-system backend identity;
- supported capabilities;
- diagnostic state.

Capabilities should be fine-grained enough that applications do not infer one
feature from another. Initial capability areas include:

- multiple windows;
- owned/modal windows;
- clipboard text;
- IME/text input;
- file drop;
- window positioning;
- theme preference;
- high contrast;
- reduced motion;
- native surface exposure;
- pointer capture;
- touch;
- pen;
- native dialogs.

Required version 0.1 capabilities are defined separately from optional
capabilities. Unsupported direct requests return `Unsupported`; they do not
silently succeed.

### Threading And Lifetime

- Platform initialization, window mutation, and event pumping occur on one
  owning platform thread.
- Every event sink invocation occurs on that thread.
- `WakeEventLoop()` is safe from other threads.
- Window destruction invalidates its generation immediately.
- Stale or foreign handles are rejected deterministically.
- Backend code must not retain borrowed spans beyond the call that supplies
  them.
- Event delivery must not re-enter arbitrary application code while internal
  backend collections are being mutated.
- Shutdown destroys input sessions, clipboard ownership, native surfaces, and
  windows before releasing the native connection.

### Event Loop

The event loop must support:

```text
Poll
WaitUntilDeadline
Wake
RequestExit
```

An idle application must block without continuous polling. The bounded wait
must integrate:

- native window events;
- clipboard and drag-and-drop transfers;
- text-input events;
- an explicit cross-thread wake source;
- future platform-owned asynchronous requests.

The contract should define event ordering within one platform queue and the
point at which events produced during a callback become observable.

### Window Lifecycle

Window creation must account for asynchronous platforms.

`CreateWindow()` returns a live logical window but does not promise that a
drawable native surface is configured immediately. The contract should include
events equivalent to:

```text
WindowConfigured
WindowSurfaceAvailable
WindowSurfaceInvalidated
WindowCloseRequested
WindowResized
WindowScaleChanged
WindowFocusChanged
WindowVisibilityChanged
```

NGIN.UI and renderers create or resize presentation surfaces only after the
window reports a valid configuration.

A close request does not destroy a window. The application decides whether to
close it.

Portable size and position operations must be separated:

```text
RequestWindowSize       required
RequestWindowPosition   optional capability
```

Wayland does not need to invent or emulate global window positions. APIs that
cannot be implemented honestly remain optional.

Window creation information should cover:

- stable authored identity;
- title;
- initial and minimum size;
- resizability;
- initial visibility;
- top-level or dialog role;
- owner;
- modality;
- optional initial state such as normal, minimized, or maximized.

### Coordinate Systems

The contract must distinguish:

- device-independent window-local coordinates;
- physical client pixels;
- physical screen coordinates where the backend exposes them;
- scale factors.

Platform geometry types should be platform-owned. NGIN.UI may convert them to
UI geometry at its boundary, but `NGIN.Platform` must not depend on NGIN.UI
geometry types.

Fractional scale factors must remain representable. Pixel extents and
renderer-facing surface sizes remain integral.

### Keyboard And Pointer Input

Keyboard events should expose:

- a stable physical key identity;
- a normalized logical key;
- press, release, and repeat state;
- independently represented modifiers;
- device identity when available.

Physical key identity should use one documented cross-platform convention
rather than leaking Win32 scan codes, X11 keycodes, or provider-specific SDL
values.

Committed text does not come from logical key events. Text and IME delivery use
the text-input session.

Pointer events should cover:

- enter and leave;
- motion and delta;
- button transitions;
- wheel or high-resolution scroll;
- pointer identity;
- mouse, touch, or pen source;
- capture gained and lost.

Version 0.1 requires complete mouse behavior. Touch and pen events may exist in
the contract only when their capability and acceptance coverage are explicit.

Native pointer capture must be represented. Logical capture inside NGIN.UI is
not sufficient when a drag leaves the operating-system window.

### Text Input And IME

The existing start/stop plus candidate-rectangle model is not sufficient for
Windows Text Services Framework or modern Linux text input.

The portable contract should use an explicit text-input session:

```text
BeginTextInput(window, client)
UpdateTextInputState(session, state)
EndTextInput(session)
```

State supplied by the client should include:

- bounded surrounding UTF-8 text;
- selection range;
- active composition range;
- caret/candidate rectangle;
- text purpose, such as normal, password, email, numeric, or search;
- multiline and read-only hints.

The platform must be able to deliver:

- pre-edit/composition replacement;
- committed UTF-8 text;
- delete-surrounding requests;
- selection updates when supported;
- composition cancellation.

All normalized text offsets use UTF-8 byte offsets. Each backend performs
explicit conversion at its native boundary.

The client contract must avoid giving the platform unrestricted access to the
entire application text model. Surrounding text should be bounded and updated
only for the active session.

### Clipboard And Data Transfer

Clipboard reads must be asynchronous because Linux clipboard protocols are
ownership- and event-loop-based.

The first contract should use a request/result flow such as:

```text
RequestClipboardText() -> ClipboardRequestId
ClipboardTextReceived { request, result }
```

Clipboard writes retain ownership until replaced, cleared, or the context
shuts down.

Version 0.1 clipboard scope is UTF-8 text. File drop normalizes supported URI or
native file offers into paths, but `NGIN.Platform` does not open or manage
those files.

General MIME data transfer and application-defined clipboard formats are
future work. The initial internal design should not assume clipboard transfers
are synchronous strings.

### Cursors, Displays, And Preferences

Required cursor behavior includes:

- arrow;
- text;
- pointer/hand;
- crosshair;
- horizontal and vertical resize;
- both diagonal resize directions;
- hidden cursor.

Display information should include:

- stable session identity;
- human-readable name when available;
- pixel bounds where meaningful;
- work area where meaningful;
- scale factor;
- primary status.

System preference events should represent:

- light, dark, or unspecified theme;
- high contrast;
- reduced motion.

Unavailable preferences remain unspecified and are reflected in capabilities
or diagnostics.

### Renderer Surface Interop

`NGIN.Platform` does not create graphics devices or choose a renderer. It
exposes the native data required for a renderer to attach to a live window.

The initial `SurfaceSource` must support:

```text
Win32:
  HWND

Wayland:
  wl_display*
  wl_surface*

X11/XCB:
  xcb_connection_t*
  xcb_window_t
  visual identity where required
```

Portable public headers should use tagged opaque values or forward-declared
interop types rather than include Win32, Wayland, or XCB headers everywhere.

The surface source has an explicit lifetime tied to its window generation.
Renderers must reject incompatible providers and stale sources.

The surface contract must be proven with a minimal consumer for Win32,
Wayland, and X11 before it is frozen. A contract that works only for SDL window
identities is not sufficient.

### Diagnostics

Diagnostics should expose:

- selected provider;
- selected Linux display backend;
- capabilities;
- live window and text-session counts;
- event and wake counters;
- clipboard and data-transfer state;
- last native error;
- optional missing protocol or extension information;
- fallback reason when Linux selects X11 instead of Wayland.

Diagnostics must not expose clipboard contents, typed text, or other private
user data.

## Native Backend Strategy

### Windows Backend

The Windows backend should use direct Windows APIs:

- User32 for window classes, windows, cursors, messages, focus, capture, and
  monitor interactions;
- per-monitor DPI APIs for scale and pixel extent;
- Text Services Framework for full text and IME behavior;
- OLE clipboard and drag-and-drop facilities;
- DWM and system settings for supported theme and preference information;
- an event or private posted message for cross-thread wake.

The backend should:

- register its window class once per owned context or process as required;
- store only a stable backend record pointer or ID in native window userdata;
- normalize all UTF-16 boundaries to UTF-8;
- preserve message order;
- avoid dispatching stale HWND messages to a new window generation;
- expose the HWND only through renderer/accessibility interop;
- support multiple simultaneous top-level and dialog windows.

Windows accessibility remains owned by its NGIN.UI provider. The platform
contract supplies the live native window association without learning UI
semantics.

### Linux Backend Selection

Linux ships one public NGIN.Platform product containing the enabled native
providers.

Automatic selection should:

1. honor an explicit application or diagnostic override;
2. attempt Wayland when the session environment and connection support it;
3. attempt X11 when Wayland initialization is unavailable or fails before
   application windows exist;
4. return a combined diagnostic when neither backend can initialize.

The implementation must not silently switch from Wayland to X11 after windows
or application-visible resources have been created.

### Linux Common Layer

Shared Linux behavior should own:

- event-loop wake integration;
- normalized key and pointer mappings;
- UTF-8 conversions;
- capability and diagnostics reporting;
- common display/backend selection policy;
- shared `libxkbcommon` integration where appropriate.

Wayland and X11 remain distinct native providers behind the common public
facade. Their native event and ownership models should not be forced into one
implementation class.

### Wayland Backend

The Wayland provider should cover:

- registry discovery and required global binding;
- `wl_compositor`;
- `xdg-shell` top-level and popup/dialog relations;
- seat, pointer, and keyboard input;
- `libxkbcommon` keymap, modifier, Compose, and dead-key behavior;
- data-device clipboard and file-drop behavior;
- available text-input protocol support;
- output tracking and fractional scale;
- cursor shape or cursor theme behavior;
- asynchronous configure and surface lifecycle;
- event-loop integration through the Wayland display file descriptor;
- explicit diagnostics for unavailable optional protocols.

Decoration policy is a dependency decision gate. The implementation must
either:

- use an approved `libdecor` dependency; or
- own and test client-side decorations.

Requiring server-side decorations is not an acceptable general Linux desktop
contract.

### X11 Backend

The X11 provider should prefer XCB-oriented integration where practical and
cover:

- XCB connection, windows, and event processing;
- ICCCM and EWMH window behavior;
- `xkbcommon-x11` keyboard state;
- XInput2 or the selected equivalent for modern pointer input;
- X selections for clipboard ownership and requests;
- XDND file drops;
- XRandR display enumeration and changes;
- XIM or an approved IBus path for text input;
- wake integration with the X connection file descriptor.

The X11 implementation must use the same portable event and text-session
contract as Wayland. Applications and NGIN.UI must not contain X11-specific
branches.

## Dependency Decision Gates

No new third-party or platform dependency should be added merely because it is
listed here. Each dependency requires an explicit approval with version,
provider, license, staging, and support policy.

Likely Linux requirements to evaluate are:

- Wayland client libraries;
- Wayland protocol definitions and scanner/code generation;
- `libxkbcommon`;
- XCB and required protocol libraries;
- `xkbcommon-x11`;
- XInput2 support;
- XRandR support;
- `libdecor`, if selected;
- an IBus or XIM integration strategy if compositor protocols alone do not
  provide acceptable IME coverage.

Required decisions before contract freeze:

1. Approve `libxkbcommon` and its provider model.
2. Choose `libdecor` or owned client-side decorations.
3. Prove the Wayland text-input path on the supported desktop matrix.
4. Choose the X11 IME path.
5. Approve the XCB/XInput/XRandR dependency set.
6. Freeze the renderer-facing native `SurfaceSource`.

Windows implementation should rely on operating-system SDK libraries rather
than add a third-party windowing dependency.

## NGIN.UI Migration

The migration should be breaking and direct. Do not keep two public platform
contracts inside NGIN.UI.

Move or replace these responsibilities currently owned by NGIN.UI:

- platform window handles;
- platform initialization;
- window creation and mutation;
- normalized platform events;
- event pumping and wake;
- cursors;
- clipboard;
- text-input lifecycle;
- display information;
- native window interop;
- platform capability negotiation.

NGIN.UI continues to own:

- device-independent UI layout geometry;
- runtime element identity;
- routed UI events;
- focus and logical pointer capture;
- editing state;
- semantic controls;
- display lists;
- renderer packets and texture resources;
- accessibility semantic snapshots.

An NGIN.UI adapter may convert platform geometry and events into UI-owned
types. The adapter should be one boundary, not per-control handlers.

The current Windows accessibility provider must migrate from UI-owned native
window information to the equivalent NGIN.Platform interop contract in the same
change that updates application creation.

## SDL Migration

The current combined SDL package should be separated conceptually into:

```text
NGIN.Platform.Backend.SDL3
NGIN.UI.Renderer.SDL3
```

The SDL platform provider implements the NGIN.Platform contract. The SDL
renderer remains a UI renderer provider.

The migration provider exists to:

- keep the gallery working while native backends are implemented;
- run the shared backend contract suite against an established implementation;
- distinguish platform regressions from renderer regressions;
- allow the NGIN.UI contract migration to land before native backend parity.

It must be explicitly selected. `NGIN.Platform` must not silently fall back to
SDL.

The current SDL renderer resolves an SDL window from the existing platform
handle. It cannot be assumed to attach to a Win32, Wayland, or X11 window
created by NGIN.Platform. Complete SDL removal from NGIN.UI therefore requires
a separate native renderer workstream.

Expected long-term renderer direction:

```text
Windows: D3D renderer provider
Linux:   Vulkan renderer provider
```

The exact graphics API and package plan is outside this document.

## Delivery Workstreams

### Workstream A — Contract And Native Spikes

Deliver:

- Win32 text-input session spike using Text Services Framework;
- Wayland asynchronous window/configure spike;
- Wayland clipboard request and ownership spike;
- X11 clipboard selection spike;
- Wayland decoration decision evidence;
- Wayland and X11 IME evidence;
- renderer surface-source spike on all three native providers.

Exit criterion:

The difficult native boundaries have executable prototypes, and the portable
contract is based on their proven requirements rather than the current SDL
shape alone.

### Workstream B — Core Package And Deterministic Backend

Deliver:

- repository and package skeleton;
- platform errors and results;
- generational handles;
- geometry and coordinate types;
- events;
- capabilities and contract version;
- context and window lifetime;
- text-input session contract;
- asynchronous clipboard contract;
- surface-source contract;
- deterministic test backend;
- focused contract tests.

Exit criterion:

A headless context creates multiple logical windows, receives injected window,
pointer, keyboard, text, clipboard, and preference events, exercises wait/wake,
then shuts down with no live resources.

### Workstream C — SDL Contract Adapter

Deliver:

- explicit `NGIN.Platform.Backend.SDL3` package;
- SDL implementation of the new platform contract;
- shared contract-suite coverage;
- NGIN.UI consumption of NGIN.Platform;
- split platform and renderer construction at the application boundary;
- standalone and hosted gallery smoke coverage.

Exit criterion:

NGIN.UI no longer owns a public platform backend contract, and the existing
gallery behavior remains available through explicitly selected SDL platform
and renderer providers.

### Workstream D — Native Win32 Provider

Deliver:

- initialization and window class;
- multiple windows and dialog ownership;
- event loop and thread-safe wake;
- resize, focus, close, visibility, and DPI events;
- keyboard and mouse input;
- native pointer capture;
- TSF text-input sessions;
- text clipboard;
- cursors;
- display enumeration;
- file drop;
- preferences;
- native surface and accessibility interop;
- native diagnostics and contract tests.

Exit criterion:

The NGIN.UI gallery completes the native Windows acceptance matrix through the
Win32 provider without using SDL for windowing or input.

### Workstream E — Native Wayland Provider

Deliver:

- connection and registry;
- asynchronous window configuration;
- multiple top-level and owned windows;
- pointer and keyboard input;
- text-input sessions;
- clipboard and file drops;
- display and scale handling;
- cursors and decorations;
- event-loop wait/wake;
- native surface interop;
- protocol diagnostics;
- shared contract-suite and native smoke coverage.

Exit criterion:

The same public gallery application completes the Linux Wayland acceptance
matrix without SDL windowing or input.

### Workstream F — Native X11 Provider

Deliver:

- XCB window and event lifecycle;
- ICCCM/EWMH integration;
- keyboard and pointer input;
- text-input sessions;
- clipboard and XDND;
- displays and DPI;
- cursors;
- event-loop wait/wake;
- native surface interop;
- shared contract-suite and native smoke coverage.

Exit criterion:

The same public gallery application completes the Linux X11 acceptance matrix
without source or manifest changes.

### Workstream G — Cross-Backend Parity And Hardening

Deliver:

- shared scenario traces across test, SDL, Win32, Wayland, and X11 providers;
- stale-handle and repeated create/destroy tests;
- multi-window stress;
- DPI/display transition tests;
- Unicode clipboard coverage;
- IME composition and cancellation coverage;
- wait/wake race coverage;
- startup and shutdown failure injection;
- capability and diagnostics audits;
- native manual checklists;
- package and install-consumer validation.

Exit criterion:

Every required version 0.1 capability passes the shared contract suite and its
native acceptance checks on Windows, Wayland, and X11.

### Workstream H — SDL Exit

Deliver:

- a renderer that accepts NGIN.Platform native surface sources on Windows;
- a renderer that accepts NGIN.Platform native surface sources on Wayland and
  X11;
- gallery and hosting migration to native platform and renderer providers;
- removal of SDL from the default NGIN.UI application path;
- retained explicit SDL packages only if they remain a deliberately supported
  alternative.

Exit criterion:

The default Windows and Linux NGIN.UI products build and run without resolving
SDL.

## Verification Strategy

### Shared Contract Suite

Every backend must run the same behavior-focused suite covering:

- contract version and capabilities;
- initialization and duplicate initialization;
- valid, stale, and foreign handles;
- multiple windows;
- owner and modal relations;
- asynchronous window configuration;
- size and supported position requests;
- close request versus destruction;
- event ordering;
- bounded wait, timeout, and cross-thread wake;
- focus and scale changes;
- pointer motion, buttons, wheel, capture, and capture loss;
- physical/logical keyboard mapping and modifiers;
- UTF-8 commit, composition, deletion, and cancellation;
- clipboard ownership and asynchronous reads;
- cursor selection;
- display enumeration;
- file drops;
- preference changes;
- surface-source lifetime;
- partial initialization and shutdown failures.

### Native Windows Coverage

Required automated or manual scenarios:

- multiple Win32 windows;
- movement between displays with different scale factors;
- mouse capture outside the client area;
- Unicode clipboard round trip;
- TSF composition with at least one CJK input method;
- owned modal dialog behavior;
- file drop;
- system theme/high-contrast change;
- accessibility provider attachment to the native window;
- clean shutdown with pending wake and clipboard operations.

### Native Linux Coverage

The supported test matrix should include:

- one current GNOME Wayland environment;
- one current KDE Wayland environment;
- one X11 environment;
- a headless Wayland compositor in CI where practical;
- Xvfb or an equivalent isolated X11 server in CI.

Required scenarios:

- Wayland automatic selection;
- explicit Wayland and X11 selection;
- X11 fallback diagnostic;
- keymap changes, modifiers, Compose, and dead keys;
- Unicode clipboard ownership and request;
- IME composition on supported desktops;
- scale and output changes;
- file drop;
- multiple windows and owned dialogs;
- client- or server-side decoration behavior;
- clean compositor/server disconnect diagnostics.

### NGIN.UI Acceptance

The same authored gallery view must:

- run through the deterministic test platform;
- run through SDL during migration;
- run through native Win32;
- run through native Wayland;
- run through native X11;
- preserve composition, layout, input, text editing, clipboard, dialogs,
  accessibility semantics, and display-list behavior.

Platform parity is measured by public behavior and explicit capabilities, not
by forcing every operating system to expose identical native mechanics.

## Version 0.1 Definition Of Done

`NGIN.Platform` version 0.1 is complete when:

- `NGIN.Platform` has no SDL dependency;
- native Win32, Wayland, and X11 providers implement every required capability;
- Linux automatically selects Wayland and falls back to X11 before creating
  application-visible resources;
- the event loop waits while idle and wakes safely from another thread;
- multiple windows and owned/modal dialogs work on all required backends;
- keyboard, mouse, UTF-8 text input, IME, clipboard, cursors, displays, DPI,
  file drop, and required preferences pass their acceptance coverage;
- unsupported operations are capability-gated and return structured errors;
- stale handles and native surface sources are rejected;
- NGIN.UI consumes the NGIN.Platform contract rather than own a duplicate
  platform backend contract;
- the headless backend provides deterministic platform tests;
- package manifests expose only the intended Windows and Linux compatibility;
- dependency licenses and notices are staged correctly;
- public headers do not require SDL, Win32, Wayland, X11, or graphics headers.

SDL removal from the default NGIN.UI runtime is a subsequent joint
platform-and-renderer exit gate, not evidence that `NGIN.Platform` itself is
incomplete.

## Risks And Mitigations

### Wayland Is Not A Win32-Shaped API

Risk:

Synchronous window readiness, global position, clipboard reads, and several
desktop behaviors do not map directly.

Mitigation:

Design asynchronous lifecycle and request contracts before implementation.
Make position and protocol-dependent behavior explicit capabilities.

### Text Input Is More Than Key Events

Risk:

A minimal candidate-rectangle contract appears to work for simple Latin input
but fails for TSF, Wayland input methods, surrounding-text operations, and
composition replacement.

Mitigation:

Prove a bounded text-input client/session contract on Windows and both Linux
display paths before freezing it.

### Linux Dependency Expansion

Risk:

Replacing SDL can accidentally reproduce a large toolkit dependency tree.

Mitigation:

Approve each dependency separately, keep it private to the native provider,
and prefer focused system-protocol libraries. Do not vendor broad desktop
toolkits for windowing.

### Decorations

Risk:

A Wayland window may require client-side decorations on environments that do
not provide server-side decorations.

Mitigation:

Make an explicit `libdecor` versus owned-decoration decision during the spike
workstream. Do not defer it until gallery integration.

### Platform And Renderer Coupling

Risk:

The current SDL renderer assumes SDL-created windows. A poorly designed
surface contract could recreate this coupling with different names.

Mitigation:

Prove `SurfaceSource` against native Windows, Wayland, and X11 consumers before
contract freeze. Keep device and presentation ownership in renderer packages.

### Current Accessibility Integration

Risk:

Moving native window ownership can break the Windows UI Automation provider or
create two competing native interop contracts.

Mitigation:

Migrate accessibility attachment in the same workstream as NGIN.UI application
creation and delete the UI-owned native window contract once consumers move.

## Decisions Required Before Implementation

1. Confirm the proposed package/source ownership layout.
2. Confirm Wayland plus X11 as the required Linux version 0.1 definition.
3. Approve the asynchronous window, clipboard, and text-session direction.
4. Choose the Linux decoration approach.
5. Choose and approve Linux protocol dependencies and providers.
6. Choose the X11 IME integration.
7. Freeze the physical key identity convention.
8. Freeze the native renderer `SurfaceSource` representation.
9. Decide whether gamepads belong in version 0.2 or a separate
   `NGIN.Input.Gamepad` package.
10. Create a separate native NGIN.UI renderer plan before scheduling the SDL
    exit workstream.

## Immediate Next Actions

1. Approve the product boundary, version 0.1 scope, and repository layout.
2. Write the public contract sketch without moving NGIN.UI code.
3. Implement the Win32 TSF, Wayland configure, Linux clipboard, Linux IME, and
   surface-source spikes.
4. Record dependency decisions with licenses and provider strategies.
5. Create `Dependencies/NGIN/NGIN.Platform` and its deterministic contract
   tests.
6. Implement the SDL adapter and migrate NGIN.UI to the new contract.
7. Implement Win32, Wayland, and X11 vertical slices.
8. Run one final parity and hardening pass.
9. Begin the separate renderer workstream required for the SDL exit gate.
