# NGIN.UI Multi-Page Example

This is the smallest complete hosted NGIN.UI application in the repository.
It demonstrates:

- Core singleton and page-scoped services;
- a reflection-free Home ViewModel;
- a reflected Detail ViewModel constructor;
- typed page registration and navigation parameters;
- back navigation;
- an asynchronous page load;
- deterministic ViewModel, task, and page-scope teardown.

Build and run it with:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.MultiPage/NGIN.UI.MultiPage.nginproj `
  --profile Debug `
  --output build/manual/NGIN.UI.MultiPage
build/manual/NGIN.UI.MultiPage/bin/NGIN.UI.MultiPage.exe
```

Pass `--smoke` to create both pages, begin the async load, and exit through the
normal cancellation and scope-teardown path.

The Views are the `ComposeHome` and `ComposeDetail` functions in
`src/main.cpp`. They own controls and layout. Their ViewModels own state and
actions. Core owns service lifetimes, and `NavigationService` owns the page
stack.
