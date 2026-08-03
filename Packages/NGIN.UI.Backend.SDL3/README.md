# NGIN.UI SDL3 backend

This package provides native windowing and rendering for `NGIN.UI` through
SDL3. It depends on the backend-neutral UI core and the repository's SDL3
source provider.

Current package version: `0.4.0`.

Use it for desktop applications that want the standard NGIN.UI backend. Use the
headless platform and recording renderer from `NGIN.UI` for deterministic unit
tests.

```xml
<Package Name="NGIN.UI.Backend.SDL3"
         Version=">=0.4.0 &lt;0.5.0"
         Scope="Target">
  <Feature Name="RuntimeNotices" />
</Package>
```

Start with the [first-window guide](../../docs/guides/ngin-ui-first-window.md)
or the runnable [Gallery](../../Examples/NGIN.UI.Gallery).

SDL notices and licenses are staged through the `RuntimeNotices` feature. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
