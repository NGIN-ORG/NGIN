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
