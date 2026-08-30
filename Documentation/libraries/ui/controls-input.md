---
title: UI controls and input
description: Compose standard controls and route pointer, keyboard, focus, text, IME, and drag-and-drop input.
---

# Controls and input

Controls combine composition, state, semantics, visual states, and input
behavior. Use a standard control before constructing the interaction manually.

## Input path

```text
platform event ─► hit testing/focus ─► routed input ─► control behavior
```

The backend translates native events. The UI core owns routing and control
semantics, which keeps application behavior independent of SDL or another
windowing implementation.

## Focus and keyboard

Keyboard interaction follows focus. Every interactive control should expose a
logical focus target, visible focus state, and semantic role. Do not require a
pointer for an operation that should be keyboard accessible.

## Text input

Text editing coordinates Unicode shaping, selection, caret behavior, clipboard,
and input-method editors. Treat text composition events differently from raw
key presses.

## Collections

Lists, menus, and virtualized controls require stable item identity. Use model
keys rather than the current array index so focus, selection, and retained
control state follow the logical item.
