# NGIN.UI.Hosting

`NGIN.UI.Hosting` connects `NGIN.UI` application lifecycle and services to an
`NGIN.Core` host. Use it when a UI application already uses the hosted runtime;
standalone UI applications do not need this package.

Current package version: `0.4.0`.

## What it provides

- registration of the UI application, window manager, dispatcher, platform,
  renderer, and service-provider bridge;
- host startup and shutdown integration;
- a UI-aware host run loop;
- scoped ViewModel creation from `NGIN.Core` services;
- typed page factories and navigation integration.

The bridge does not move UI work to arbitrary host threads. UI objects retain
their normal dispatcher and window lifetime rules.

## Start here

- [Hosted first window](../../docs/guides/ngin-ui-hosted-first-window.md)
- [Application composition](../../docs/guides/ngin-ui-application-composition.md)
- [Dependency injection](../../docs/guides/ngin-core-di.md)
- [Hosted Gallery](../../Examples/NGIN.UI.Gallery.Hosted)

The package depends on `NGIN.Core` and `NGIN.UI`. A concrete platform and
renderer backend, such as `NGIN.UI.Backend.SDL3`, is still required for native
windows.
