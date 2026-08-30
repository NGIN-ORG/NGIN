---
title: UI backends and hosting
description: Select native rendering, accessibility, and optional NGIN.Core hosting integrations through packages.
---

# Backends and hosting

NGIN.UI keeps application composition independent of native windowing and
rendering. Packages supply concrete backend capabilities.

## SDL3 backend

`NGIN.UI.Backend.SDL3` provides native windows and rendering through SDL3 and
SDL_GPU. Products normally request the `NGIN.UI.Backend` capability, allowing
the workspace to select the provider.

```xml
<Uses>
  <Package Name="NGIN.UI" Version="0.4" />
  <Capability Name="NGIN.UI.Backend" Version="1" />
</Uses>
```

## Windows accessibility

`NGIN.UI.Accessibility.Windows` maps the backend-neutral semantic tree to
Windows UI Automation. It belongs in the platform-specific package graph, not
in portable view code.

## NGIN.Core hosting

`NGIN.UI.Hosting` integrates application and window lifetime with NGIN.Core.
Use it when the application is already hosted; standalone UI programs can own
`Application` directly.

## Backend authoring rule

A backend translates platform operations and events. It should not own
application navigation, view models, product configuration, or another copy of
control behavior.
