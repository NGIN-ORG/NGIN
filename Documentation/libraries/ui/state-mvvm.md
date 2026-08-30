---
title: UI state and MVVM
description: Drive composition through state, bindings, validation, commands, view models, and navigation.
---

# State and MVVM

Application state lives outside the composition callback. Mutation invalidates
the appropriate part of the retained view.

## Local state

Use `State<T>` for focused view state and expose controlled mutation through a
binding:

```cpp
State<NGIN::Text::String> name{
    NGIN::Text::String{"Ada"},
    [window](InvalidationKind kind) { window->Invalidate(kind); }};

// Inside composition:
composer.TextField(Bind(name), *text, fieldProperties, "name");
```

## View models

Use a view model when state and actions form a reusable screen-level contract.
Keep platform and rendering objects outside the view model so it remains
testable and backend-neutral.

## Commands

A command represents an action and whether it can currently execute. Controls
can reflect disabled state from that contract without duplicating business
rules in the view.

## Validation

Separate raw editing state from validated domain state when temporary invalid
input is expected. Present errors through both visual feedback and accessible
semantics.

## Navigation

Navigation owns page identity and lifetime. Define whether navigating away
discards, caches, or preserves the view model, and cancel page-owned work when
its lifetime ends.
