# NGIN.UI Input, Accessibility, and Tooling

This guide covers routed input, focus, standard commands, themes, resource
scopes, semantics, the inspector, and thread-affinity rules.

Use the buildable
[`NGIN.UI.Gallery`](../../Examples/NGIN.UI.Gallery/) Inputs, Resources, and
Diagnostics pages alongside this guide.

## Routed input

Pointer, key, and text events route through the retained tree in three phases:

1. capture from the root toward the target;
2. target delivery;
3. bubble from the target toward the root.

Callbacks receive `RoutedPointerEvent`, `RoutedKeyEvent`, or
`RoutedTextEvent`. Set `handled = true` to suppress the control's fallback
behavior and later default routing. A callback may still observe an event that
an earlier phase handled, so check the flag before acting.

Pointer coordinates are window-local device-independent units. Pointer capture
keeps move/release delivery on the captured element until release, cancellation,
unmount, disablement, or visibility loss.

Custom-painted controls receive the same routed events through
`ICustomElement::Input()`. Composite controls normally attach callbacks to
ordinary semantic elements.

## Hit testing

Hit testing walks reverse paint order. An element participates only when it is
visible, enabled for hit testing, inside inherited clips, and under the pointer.
Popups are tested above normal content. A modal popup blocks the underlying
tree even when no popup child is hit.

Set `interaction.hitTestVisible = false` on decorative text and shapes so the
interactive ancestor remains the target.

## Focus and keyboard behavior

Focusable elements enter deterministic tab order by tree order and `tabIndex`.
Disabled, hidden, collapsed, semantically hidden, or unmounted nodes are
excluded. Focus is restored safely when a modal popup or dialog closes.

Built-in keyboard conventions include:

- Enter/Space activation for buttons, list items, tabs, and menu items;
- arrows plus Home/End for lists, tabs, menus, and scroll views;
- timed type-ahead for lists;
- Tab and Shift+Tab traversal;
- Escape dismissal for eligible popups;
- standard editing movement and deletion for text controls;
- line-relative Home/End and vertical caret movement for `TextArea`.

Do not implement a second global command handler for behavior the focused
control already owns. Use a routed key handler when a scope needs to intercept
or augment it.

## Clipboard and IME

Text controls use Control/Super+A/C/X/V according to the normalized command
modifier. Clipboard support is capability-negotiated through
`IPlatformBackend`; an unsupported operation reports a structured error.

IME composition is transient:

- `TextComposition` updates candidate text and its selected byte range;
- the bound value does not change during composition;
- `TextInput` commits the candidate transactionally;
- focus loss cancels an active candidate;
- candidate rectangles are the focused control bounds scaled to pixels.

Composition offsets use UTF-8 bytes because that is the platform boundary.
Editing state and selections use grapheme-cluster indices.

## Themes and visual states

`Theme` is an immutable value bundle. Controls resolve a base style, then state
patches in deterministic order. Use the helpers in `Theme.hpp` instead of
copying gallery colors.

See the [styling guide](ngin-ui-styling.md) for state precedence, focus visuals,
borders, and invalidation. The gallery's light/dark switch proves that the
whole view can recompose from a different theme value.

## Typed resource scopes

`ResourceScope` is an immutable typed parent chain:

```cpp
auto resources = std::make_shared<ResourceScope>();
resources = resources->With<Theme>(theme);
resources = resources->With<AppLocale>(AppLocale{"sv-SE"});

composer.Scope(resources, [&] { ComposeSettings(composer); }, "settings");
```

Lookup starts at the node and follows parents. Keep frequently changing model
data in `State<T>`; resource scopes are for shared contextual values.

## Semantics

Every interactive control must expose:

- the closest accurate `SemanticRole`;
- a visible or semantic label;
- current value/range/state where applicable;
- supported actions;
- meaningful descriptions for informative images and custom visuals.

Use `labelFor`, `labelledBy`, and stable identifiers for explicit
relationships. Password controls deliberately suppress semantic values.
Decorative nodes should set `semantics.hidden = true`.

`BuildSemanticTree()` mirrors visible runtime ownership with stable IDs derived
from retained element identity. That makes key stability important for assistive
technology as well as control state.

## Inspector and diagnostics

`Window::Inspect()` returns an immutable snapshot of runtime and semantic trees.
`SetInspectorOverlay()` can draw layout bounds, hit-test bounds, focus, and
selection without modifying the view.

`Window::Diagnostics()` reports node counts, display commands, focus/capture,
reconcile/layout work, and frame-phase timings. Log these before guessing at a
layout or rendering issue.

The gallery Diagnostics page is a live example; the headless gallery asserts
semantic roles on every control page.

## UI-thread rule

Treat these as UI-thread-only unless an interface explicitly says otherwise:

- `Application`, `Window`, `Composer`, `RuntimeTree`, and `InputRouter`;
- platform and render backends;
- `NativeTextSystem` and `ImageTextureCache`;
- model mutation that triggers window invalidation.

Standalone applications normally perform all UI work from event callbacks or
`Window::Schedule()`. Hosted applications post through
`NGIN::UI::Hosting::IUIDispatcher`.

Workers may own ordinary values and logical `ImageResource` decode work. They
must not retain a composer, node pointer, backend handle, or renderer-bound
cache. Post the completed value back, update model state, then invalidate.

## Error handling

Public fallible operations return `UIResult<T>` and never require parsing an
error string. Log:

- `code`;
- `backend`;
- `operation`;
- `logicalResource`;
- `nativeCode`;
- `message`.

Property callbacks receive errors from otherwise void composition/layout paths.
Do not throw through input, paint, or lifecycle callbacks; custom-element
exceptions are converted to structured errors, but ordinary handlers should
still report failures explicitly.
