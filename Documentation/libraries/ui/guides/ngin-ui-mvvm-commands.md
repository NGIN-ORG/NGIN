# NGIN.UI MVVM Commands

NGIN.UI Views remain synchronous composition functions. ViewModels own state
and commands, and Models remain ordinary C++ types.

Use a `Command` for an immediate action and `AsyncCommand` for work that may
wait. Both expose a lifetime-safe `CommandBinding` that controls can retain.

## Synchronous commands

Construct a command with an action and optional invalidation scheduler:

```cpp
class CounterViewModel final {
public:
  explicit CounterViewModel(Window &window)
      : increment(
            [this] { ++count; }, true,
            [&window](InvalidationKind kind) { window.Invalidate(kind); }) {}

  int count{0};
  Command increment;
};
```

`SetEnabled(false)` changes `CanExecute` and schedules composition. A command
that needs to reject an action can return `CommandResult<void>` with a
`CommandError`; the error is retained in `CommandStatus::lastOutcome`.

Ordinary callback button overloads remain available. Use a command only when
the action needs observable availability or outcome state.

## Asynchronous commands

An async command action returns `Task<void, CommandError>` and receives the
context on which it runs:

```cpp
class EditorViewModel final {
public:
  EditorViewModel(Application &application, Window &window)
      : save(
            application.CreateTaskContext(window),
            [this](NGIN::Async::TaskContext &context) {
              return SaveAsync(context);
            },
            true, CommandConcurrencyPolicy::Reject, 1,
            [&window](InvalidationKind kind) { window.Invalidate(kind); }) {}

  auto SaveAsync(NGIN::Async::TaskContext &context)
      -> NGIN::Async::Task<void, CommandError> {
    co_await context.YieldNow();

    if (!CanStoreDocument()) {
      co_await NGIN::Async::DomainFailure(CommandError{
          .kind = CommandErrorKind::Domain,
          .code = NGIN::Text::String{"save-rejected"},
          .message = NGIN::Text::String{"The document could not be saved"},
      });
    }
  }

  AsyncCommand save;
};
```

`CommandStatus` exposes:

- `canExecute`;
- `isRunning`;
- `canCancel`;
- `queuedCount`;
- `executionId`;
- `lastOutcome` and its optional error.

Failures and thrown exceptions become observable outcomes. They are not lost
through an unobserved detached task.

## Compose a command button

The leaf form is useful for a custom-painted button:

```cpp
composer.Button(viewModel.save.AsBinding(), buttonProperties, "save");
```

Use `BeginButton` when the button contains a text label or other children:

```cpp
{
  auto button = composer.BeginButton(
      viewModel.save.AsBinding(), buttonProperties, "save");
  composer.Text(
      NGIN::Text::String{"Save"}, textLayout, glyphAtlas, labelProperties,
      "label");
}
```

The Composer reads `canExecute`, combines it with the authored enabled state,
and installs a safe activation callback. If the owning command is destroyed,
the retained binding becomes disabled and rejects activation instead of
calling stale memory.

Views may read status to present progress, cancellation, and errors:

```cpp
const auto &status = viewModel.save.Status();
if (status.isRunning) {
  ComposeProgress(composer);
}
if (status.lastOutcome.error) {
  ComposeError(composer, status.lastOutcome.error->message);
}
```

Do not retain a `Composer` or suspend composition. The command updates its
observable status, invalidates the window, and the next synchronous
composition pass presents the new state.

## Cancellation and concurrency

`Cancel()` clears queued executions and requests cancellation of the active
execution. The action must use the provided context for awaits so cancellation
can reach it.

Choose one policy when constructing an `AsyncCommand`:

- `Reject`: disable execution while busy. This is the default and prevents
  accidental double submission.
- `CancelPrevious`: cancel the old execution and start the new one.
- `Queue`: retain a bounded number of later executions.

`CommandInvocation` reports whether an invocation started, queued, replaced an
older run, or was rejected.

## Lifetime

Use `application.CreateTaskContext(window)` for window-owned ViewModels. The
context is canceled when the command is canceled, its command owner is
destroyed, the window closes, or the application shuts down.

Use `application.CreateTaskContext()` only for work intentionally owned by the
whole application. Do not execute a command through an application context
after that application has been destroyed.
