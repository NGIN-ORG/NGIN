---
title: UI composition and layout
description: Build retained keyed views and arrange them through backend-neutral layout primitives.
---

# Composition and layout

NGIN.UI uses retained composition with keyed reconciliation. A content callback
describes the desired view; stable keys let the runtime preserve identity
across recomposition.

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

## Practical rules

- Put spacing policy on the container that owns the relationship.
- Avoid fixed pixels when content or localization should determine size.
- Use scrolling only around the region intended to exceed available space.
- Diagnose a zero-sized child from its constraints and measured result before
  treating it as a rendering problem.

## Retained captures

`SetContent()` callbacks are retained, not one-shot. Captured objects must
outlive every later invocation or be held through an appropriate owning value.
