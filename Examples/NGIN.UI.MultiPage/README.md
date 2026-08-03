# NGIN.UI multi-page application

A small hosted UI application demonstrating services, page-scoped ViewModels,
typed navigation parameters, back navigation, async loading, and deterministic
scope teardown.

```bash
ngin build --project Examples/NGIN.UI.MultiPage/NGIN.UI.MultiPage.nginproj --profile Debug --output build/manual/NGIN.UI.MultiPage
ngin run --project Examples/NGIN.UI.MultiPage/NGIN.UI.MultiPage.nginproj --profile Debug --output build/manual/NGIN.UI.MultiPage
```

Pass `--smoke` after `--` to create both pages, begin async loading, and exit
through the normal cancellation path.

The `ComposeHome` and `ComposeDetail` functions own views, ViewModels own state
and actions, `NGIN.Core` owns service lifetimes, and the navigation service owns
the page stack.
