# NGIN.UI 0.3 release

Version 0.3 adds the typed MVVM layer built on the existing synchronous
composition runtime. The NGIN.UI core, SDL3 backend, hosting bridge, Windows
accessibility provider, and Gallery products are version `0.3.0`.
Applications should request `>=0.3.0 <0.4.0` for NGIN.UI-family packages.

## What is new

- typed synchronous and asynchronous commands with availability, progress,
  cancellation, concurrency policy, and retained errors;
- read-only and computed state, batched updates, field validation, and form
  summaries;
- ViewModel-owned task scopes, keyed lifecycle hosts, small typed service
  factories, and standard idle/loading/content/empty/error presentation;
- subscription lifetime diagnostics and published task, subscription, and
  composition budgets;
- one complete Gallery form showing validation, disabled-until-valid Save,
  progress, cancellation, simulated failure, retry, and success.

Start with the [MVVM architecture guide](ngin-ui-mvvm.md). The
[command guide](ngin-ui-mvvm-commands.md),
[state and validation guide](ngin-ui-state-validation.md), and
[lifetime guide](ngin-ui-viewmodel-lifetime.md) contain focused examples.

## Migration from 0.2

No installed 0.2 UI contract was renamed or removed. Update package ranges and
the CMake package request to 0.3. The Gallery's source-only `Model` example is
now named `GalleryViewModel`; this is not an installed API.

No ViewModel base class or generator is required. Existing callbacks and
`Binding<T>` code continue to work. Adopt commands, validation, and task scopes
only where their observable status or lifetime ownership is useful.

## Release gates

The core test and benchmark suite, API documentation checker, and independent
contracts-only install consumer run on Windows, Linux, and macOS. The Gallery
CI matrix builds and runs standalone, hosted, and headless products on all
three systems; Linux native smoke uses Xvfb.

Performance and resource limits remain the 0.2 budgets. The added MVVM budgets
require zero outstanding ViewModel tasks after teardown, subscription counts
to return to their baseline, one publication/recomputation per changed value
in an outer `StateBatch`, and zero native-backend dependencies in the install
consumer. See [testing and release gates](ngin-ui-testing-and-release.md).

Build the Gallery with:

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery/NGIN.UI.Gallery.nginproj `
  --configuration Release `
  --output build/release/NGIN.UI.Gallery-0.3.0
```

Run `bin/NGIN.UI.Gallery.exe --page Inputs` to open the complete MVVM save
workflow directly, or use `--smoke` to compose every page and exit.
