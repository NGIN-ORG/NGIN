---
title: UI testing and accessibility
description: Verify retained interfaces with deterministic headless backends, semantics, snapshots, and diagnostics.
---

# Testing and accessibility

NGIN.UI includes deterministic headless backends so view behavior can be tested
without creating an operating-system window.

## Test layers

| Layer | Verify |
| --- | --- |
| State/view-model tests | Domain state, commands, validation, and navigation |
| Headless composition tests | Tree shape, keys, layout, focus, and routed input |
| Semantic tests | Roles, names, values, actions, and relationships |
| Backend integration tests | Native window, renderer, text, and platform translation |
| End-to-end tests | Complete staged application behavior |

## Accessibility tree

Semantic roles and accessible names belong in the backend-neutral view. The
Windows accessibility package maps that tree to UI Automation. A backend may
expose the same semantics through another native accessibility API.

## Prefer semantic assertions

Assert that a button has the expected role, name, state, and action before
depending on a pixel snapshot. Visual snapshots remain useful for rendering
regressions but are expensive as the only behavioral oracle.

## Diagnostics

Inspector snapshots and structured diagnostics should identify the element key,
layout constraints, semantic state, and backend operation involved. Preserve
that context when surfacing failures in tests or logs.
