# NGIN.UI Gallery tests

This test product renders every Gallery page with NGIN.UI's deterministic
headless platform and recording renderer. It also checks themes, popups,
inspector state, auxiliary windows, and modal dialogs without opening native
windows.

```bash
ngin test --project Examples/NGIN.UI.Gallery.Tests/NGIN.UI.Gallery.Tests.nginproj --configuration Debug
```

The interactive companion is the [NGIN.UI Gallery](../NGIN.UI.Gallery).

The test joins application background work before releasing its model and text
resources, including when a gallery assertion fails.
