# Windows Accessibility

NGIN.UI can expose the same semantic tree used by its controls to Windows UI
Automation. Windows Narrator and other accessibility clients can then read and
operate an NGIN.UI window without access to its runtime nodes.

## Enable the Windows provider

Add `NGIN.UI.Accessibility.Windows` to the application's package uses and pass
the factory result when creating the application:

```cpp
#include <NGIN/UI/Accessibility/Windows/Windows.hpp>

auto application = NGIN::UI::CreateApplication({
    .platform = NGIN::UI::SDL3::CreatePlatformBackend(),
    .renderer = NGIN::UI::SDL3::CreateRendererBackend(),
    .applicationName = NGIN::Text::String{"My app"},
    .accessibility = NGIN::UI::Accessibility::Windows::
        CreateAccessibilityBackend(),
});
```

The factory returns no provider outside Windows. The application remains
usable and `Application::AccessibilityDiagnostics()` reports that no native
bridge is available.

## What is exposed

Each window publishes an immutable snapshot containing stable node identity,
parent and child relationships, role, name, description, value, range, state,
logical bounds, collection position, live-region setting, and supported
actions. The Windows package maps that data to UI Automation control types,
properties, fragments, and these patterns when the node supports them:

- Invoke, Toggle, SelectionItem, RangeValue, Value, ExpandCollapse, and
  ScrollItem;
- focus, property, structure, selection, and live-region change events;
- screen-relative bounds adjusted for the window DPI.

Passwords expose the password property but never publish their text value.
Detached windows and removed semantic nodes return
`UIA_E_ELEMENTNOTAVAILABLE`. Provider actions are queued and executed through
the normal control input path on the UI thread.

## Custom controls

A custom control supplies its role, name, state, range, and actions from
`CustomElement::Semantics()`. It handles provider requests in
`CustomElement::SemanticAction()`:

```cpp
auto VolumeKnob::Semantics(NGIN::UI::CustomElementContext &)
    -> NGIN::UI::UIResult<NGIN::UI::SemanticProperties> {
  return NGIN::UI::SemanticProperties{
      .role = NGIN::UI::SemanticRole::Slider,
      .label = NGIN::Text::String{"Volume"},
      .range = NGIN::UI::SemanticRange{
          .minimum = 0.0, .maximum = 100.0, .current = m_value, .step = 1.0},
      .actions = NGIN::UI::SemanticActionFlags::Focus |
                 NGIN::UI::SemanticActionFlags::SetValue,
  };
}

auto VolumeKnob::SemanticAction(
    NGIN::UI::CustomElementContext &,
    const NGIN::UI::SemanticActionRequest &request)
    -> NGIN::UI::UIResult<NGIN::UI::InvalidationKind> {
  if (request.action != NGIN::UI::SemanticActionKind::SetValue) {
    return NGIN::UI::MakeUIError(
        NGIN::UI::UIErrorCode::Unsupported, "Action is not supported");
  }
  m_value = std::clamp(request.numericValue, 0.0, 100.0);
  return NGIN::UI::InvalidationKind::All;
}
```

Only advertise actions the control actually implements. Native accessibility,
keyboard input, and pointer input should all update the same state and use the
same validation path.

## Narrator acceptance checklist

Build and open the Gallery directly on its Accessibility page:

```powershell
build/manual/NGIN.UI.Gallery/bin/NGIN.UI.Gallery.exe --page Accessibility
```

Then start Narrator with `Windows+Ctrl+Enter` and check:

1. Narrator announces the window and the **Accessibility** heading.
2. `Tab` moves through the sidebar and every enabled control in a clear order.
3. The demo button is announced as a button and `Enter` or `Space` activates
   it.
4. The checkbox and switch announce their name and current checked state;
   `Space` changes that state.
5. The slider announces its name and value; arrow keys change the value.
6. The display-name field is announced as editable and does not expose the
   value of password fields elsewhere in the Gallery.
7. Activating **Announce a message** reads the changed polite live region.
8. Lists and tabs on the Collections page announce item selection.
9. The dialog on the Windows page has its own accessible root and focus returns
   to the main window after it closes.
10. Closing an auxiliary window does not leave navigable stale elements.

The Gallery's provider card must say **Ready: Windows UI Automation** on
Windows. If it does not, inspect `Application::AccessibilityDiagnostics()` and
its `lastError`; do not treat a missing provider as a successful accessibility
test.

macOS Accessibility and Linux AT-SPI providers are follow-up work. They will
consume the same snapshots and do not require platform-specific control
composition.
