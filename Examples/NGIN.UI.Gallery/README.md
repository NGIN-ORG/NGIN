# NGIN.UI Gallery

The Gallery is the main interactive catalogue for `NGIN.UI`.

![NGIN.UI Gallery](docs/gallery-overview.png)

It covers layout, typography, text editing, images, inputs, async data,
collections, virtualization, motion, overlays, windows, themes, accessibility,
and diagnostics. Each page includes a small public API example.

```bash
ngin build --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj --configuration Debug --output build/manual/NGIN.UI.Gallery
ngin run --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj --configuration Debug --output build/manual/NGIN.UI.Gallery
```

Useful application arguments:

```text
--page Collections       open one catalogue page
--page Accessibility     open the accessibility page
--smoke                  render every page, then exit
```

The [hosted Gallery](../NGIN.UI.Gallery.Hosted) uses the same screens through
`NGIN.Core`. The [headless test product](../NGIN.UI.Gallery.Tests) renders the
catalogue through deterministic test backends.

The `Release` profile can publish the versioned demo archive described in the
[0.4 release notes](../../docs/guides/ngin-ui-v0.4-release.md).
