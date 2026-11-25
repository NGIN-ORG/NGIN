# NGIN.UI SDL3 backend

This package provides native windowing and rendering for `NGIN.UI` through
SDL3. It depends on the backend-neutral UI core and the repository's SDL3
source provider.

Current package version: `0.4.0`.

Use it for desktop applications that want the standard NGIN.UI backend. Use the
headless platform and recording renderer from `NGIN.UI` for deterministic unit
tests.

```xml
<Capability Name="NGIN.UI.Backend" Version="1" />
```

Start with the [first-window guide](../../docs/guides/ngin-ui-first-window.md)
or the runnable [Gallery](../../Examples/NGIN.UI.Gallery).

For a component-based desktop application, include `NGIN/UI/Backend/SDL3/Desktop.hpp`
and pass a window description and view to `SDL3::Run`. For a game or editor that
already owns an `SDL_Renderer`, use `SDL3::CreateOverlayBackend` and submit a
retained surface to the supplied target. See the
[dual-host example](../../Examples/NGIN.UI.Settings/).

SDL notices and licenses are required package contributions and are staged
whenever the backend export is active. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
