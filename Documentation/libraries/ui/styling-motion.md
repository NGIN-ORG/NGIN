---
title: UI styling and motion
description: Apply themes, visual states, images, transitions, and custom control rendering.
---

# Styling and motion

Styling maps semantic control state to visual properties. Keep behavior and
meaning independent of a specific color palette or renderer.

## Themes

A theme provides shared tokens and control defaults. Use semantic roles such as
surface, foreground, accent, warning, and focus instead of copying literal
colors throughout an application.

## Visual states

Controls may react to states such as normal, pointer-over, pressed, focused,
selected, disabled, invalid, or busy. State combinations should remain legible
in both light and dark themes.

## Motion

Motion should explain change: entry, exit, selection, reordering, or progress.
Keep essential information available when motion is reduced or disabled.

## Images and custom controls

Image decoding and rendering stay behind backend-neutral resources. A custom
control should provide composition, measurement, input, and semantics as one
coherent contract rather than drawing pixels without accessibility behavior.
