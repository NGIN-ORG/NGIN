# NGIN.UI Version 0.3 Progress

Roadmap: [`NGIN.UI-V0.3-Roadmap.md`](NGIN.UI-V0.3-Roadmap.md)

## Milestone 26 — Commands And AsyncCommand

Status: Complete
Completed: 2026-08-02

Delivered:

- added synchronous `Command` with observable availability, structured domain
  errors, exception containment, and retained outcomes;
- added `AsyncCommand` on `NGIN::Async::Task` and `TaskContext` with running,
  cancellation, queue, execution identity, and outcome state;
- added reject, cancel-previous, and bounded-queue concurrency policies;
- added lifetime-safe `CommandBinding` so retained controls cannot invoke a
  destroyed command;
- added command-aware `Composer::Button` and `Composer::BeginButton` overloads
  that combine command availability with authored control state;
- added window-owned application task contexts and cancellation on explicit
  command cancellation, command destruction, window closure, and application
  shutdown;
- added deterministic coverage for success, domain failure, thrown exception,
  cancellation, repeated execution, destruction, window closure, application
  shutdown, subscriptions, and control integration;
- added an Overview Gallery card that visibly demonstrates run, fail, retry,
  cancellation, disabled-while-running behavior, and retained status;
- published the MVVM command guide and API entry-point documentation.

Verification:

- `cmake --build build/ngin-ui --config Debug --target NGINUITests` passed;
- focused `*command*` tests passed with 77 assertions in 9 test cases;
- the authored `NGIN.UI.Gallery.nginproj` Debug build passed through `ngin`;
- the staged native Gallery `--smoke` run passed across every page.

## Milestone 27 — Derived State And Validation

Status: Complete
Completed: 2026-08-02

Delivered:

- added lifetime-safe `ReadOnlyBinding<T>` views and conversion from ordinary
  state and writable bindings;
- added explicitly dependent `ComputedState<T>` with equality suppression,
  deterministic propagation, retained binding lifetime, and cycle rejection;
- added nested `StateBatch` updates that coalesce state notification,
  recomputation, and invalidation by observable identity;
- added stable validation issues with field, message, and severity plus
  immediate, deferred, and submit-time field policies;
- added cancellation-safe asynchronous validation with per-value revisions so
  stale or late results cannot replace newer input;
- added ordered `ValidationForm` summaries, aggregate validity and pending
  state, and read-only validity binding for commands without ownership cycles;
- added focused tests for dependencies, batching, cycles, retained teardown,
  policy behavior, async races, destruction, summary ordering, and command
  gating;
- added an Inputs Gallery form showing live, requested, submit-time, async,
  summary, and disabled-until-valid Save behavior;
- published the derived-state and validation guide and API entry points.

Verification:

- `cmake --build build/ngin-ui --config Debug --target NGINUITests` passed,
  and the full suite passed with 5,033 assertions in 169 test cases;
- focused state and validation tests passed with 240 assertions in 26 test
  cases;
- the authored `NGIN.UI.Gallery.nginproj` Debug build passed through `ngin`;
- the staged native Gallery `--smoke` run passed across every page.

## Milestone 28 — ViewModel Lifetime And Async Presentation

Status: Complete
Completed: 2026-08-02

Delivered:

- added `ViewModelTaskScope` with linked UI scheduling, whole-scope and
  per-operation cancellation, retained operation ownership, completion
  observation, structured errors, and read-only diagnostics;
- added `KeyedViewModelHost<T>` for factory creation, same-key reuse, keyed
  replacement, activation, deactivation, non-blocking async cleanup, and plain
  C++ ViewModels without mandatory inheritance;
- added a narrow non-owning typed service resolver hook for factories without
  introducing a container or router;
- added `AsyncPresentation<T>` for idle, loading, content, empty, and error
  state with retry and cancel command bindings;
- documented explicit unmount, tab, popup, replacement, window, and
  application lifetime rules while keeping composition synchronous;
- added deterministic coverage for completion, failure, individual and scope
  cancellation, destruction, late callbacks, keyed reuse, rapid replacement,
  cleanup, service resolution, window closure, and application shutdown;
- added an Async Data Gallery page showing delayed loading, successful
  content, empty data, failure, retry, cancellation, and rapid screen changes.

Verification:

- `cmake --build build/ngin-ui --config Debug --target NGINUITests` passed,
  and the full suite passed with 5,105 assertions in 178 test cases;
- focused ViewModel and async-presentation tests passed with 72 assertions in
  9 test cases;
- the authored `NGIN.UI.Gallery.nginproj` Debug build passed through `ngin`;
- the staged native Gallery `--smoke` run passed across all 14 pages;
- the authored headless Gallery product built through `ngin` and all
  application-level checks passed.

## Milestone 29 — MVVM Product Completion

Status: Complete
Completed: 2026-08-02

Delivered:

- renamed the shared Gallery state owner from generic `Model` to
  `GalleryViewModel` across standalone, hosted, and headless products;
- turned the Inputs validation card into a complete asynchronous save workflow
  with editing, immediate/deferred/submit validation, `CanExecute` gating,
  visible progress, double-submit protection, cancellation, simulated domain
  failure, retry, retained status, and successful completion;
- published one concise MVVM architecture guide that says the View owns
  controls and layout and defines Model, ViewModel, Composer, state, bindings,
  commands, validation, task scopes, async presentation, and app organization;
- linked copyable command, cancellation, validation, loading, retry, and error
  examples to the complete public Gallery source;
- recorded that 0.3 keeps boilerplate reduction optional: no generator is
  shipped, and any future generator must emit the manual public typed APIs;
- added process-wide subscription lifetime diagnostics with active, peak,
  created, and canceled counters plus a teardown-baseline regression test;
- published explicit outstanding-task, subscription, batched-composition, and
  backend-neutral dependency budgets and the Windows/Linux/macOS Gallery smoke
  matrix;
- expanded the independent install consumer to compile and execute State,
  Command, ViewModel, and diagnostic contracts while linking only `NGIN::UI`;
- versioned the NGIN.UI package family, Gallery products, header constants,
  CMake identities, dependency ranges, CI outputs, and release guide as 0.3.0;
- documented the public MVVM source-compatibility surface and the example-only
  Gallery rename.

Verification:

- `NGINUITests` passed with 5,111 assertions in 179 test cases; the focused
  subscription diagnostic test passed with 6 assertions;
- the API documentation gate passed with 358 documented public types;
- standalone, hosted, and headless Gallery products built from their authored
  0.3 V4 projects; native `--smoke` passed for standalone and hosted on
  Windows, and the headless checks passed;
- a fresh contracts-only NGIN.UI install with native text and standard image
  decoding disabled built successfully, and the independent
  `find_package(NGINUI 0.3)` consumer passed 1/1;
- the committed CI matrix covers standalone, hosted, and headless Gallery
  smoke on Windows, Linux under Xvfb, and macOS; this Windows session did not
  execute the other operating systems.
