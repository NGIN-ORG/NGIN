# NGIN.UI multi-page application

A small hosted UI application demonstrating services, page-scoped ViewModels,
typed navigation parameters, back navigation, async loading, and deterministic
scope teardown.

```bash
ngin build --project Examples/NGIN.UI.MultiPage/NGIN.UI.MultiPage.nginproj --configuration Debug --output build/manual/NGIN.UI.MultiPage
ngin run --project Examples/NGIN.UI.MultiPage/NGIN.UI.MultiPage.nginproj --configuration Debug --output build/manual/NGIN.UI.MultiPage
```

Pass `--smoke` after `--` to create both pages, begin async loading, and exit
through the normal cancellation path.

The page registry pairs each route with its ViewModel and a function that returns
a component tree. `DetailView` uses the typed navigation parameter for its title.
Its status component reads observable ViewModel state and updates when the
page-scoped loading task completes. The window mounts
`NavigationHost::ContentView()` once. Navigating changes the active page and
leaving it cancels pending work.
