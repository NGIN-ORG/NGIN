---
title: UI composition and layout
description: Build retained keyed views and arrange them through backend-neutral layout primitives.
---

# Composition and layout

Build a screen as an ordinary function returning `Views::View`. Use
`Views::Component` around a part of the screen that reads observable state.
It rebuilds when a dependency changes. Use `scope.Local` for component-owned
state and `.Key(itemId)` for children in a reorderable collection.

## Composition

```text
state change ─► invalidate ─► compose desired tree
                                  │
                             reconcile keys
                                  │
                           update retained tree
```

Use a stable key for each logical element. A key should identify the item, not
its current position in a list.

## Layout

Layout primitives include stacks, Grid, WrapPanel, Canvas, scrolling, padding,
gaps, alignment, and constraints. Parent constraints flow down; measured sizes
flow up; arranged positions flow down again.

A custom container implements `ILayoutPolicy` to measure and place its direct
children. Pass it to `Views::Layout`. A two-child `Views::SplitPane` demonstrates
pointer capture, keyboard resizing, and semantic actions. Use
`Views::Custom` for a leaf with custom painting.

## Windows and engine surfaces

A `Surface` owns the mounted tree and produces immutable render packets. The
desktop host drives the window's surface. An editor or game can supply its own
input, viewport, scale, and clock, then draw the packet over its scene. An idle
update returns the same packet. `SDL3::CreateOverlayBackend` submits it to an
existing SDL renderer and leaves presentation to its owner. Both paths are
exercised by the [settings example](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Settings).

## Practical rules

- Put spacing policy on the container that owns the relationship.
- Avoid fixed pixels when content or localization should determine size.
- Use scrolling only around the region intended to exceed available space.
- Diagnose a zero-sized child from its constraints and measured result before
  treating it as a rendering problem.

## Retained captures

A window or engine surface retains the view passed to `SetContent(view, environment)`.
Component factories and input callbacks can run throughout its mounted lifetime.
Keep borrowed models and services alive until the view unmounts.
