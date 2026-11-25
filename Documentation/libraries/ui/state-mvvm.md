---
title: UI state and MVVM
description: Drive composition through state, bindings, validation, commands, view models, and navigation.
---

# State and MVVM

Application state lives outside the composition callback. Mutation invalidates
the appropriate part of the retained view.

## Local state

Use `ComponentScope::Local` for a value owned by one mounted editor. For
shared form data, supply a binding from a model-owned `State<T>`.

```cpp
auto profile = Views::Component([](Views::ComponentScope &scope) {
  auto name = scope.Local("name", NGIN::Text::String{"Ada"});
  return Views::TextField("Name", name);
});
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
