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
