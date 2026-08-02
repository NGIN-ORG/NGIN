# ViewModel task lifetime and async screens

`ViewModelTaskScope` gives asynchronous ViewModel work one owner. It creates a
child `TaskContext`, tracks every task started through it, observes every
completion, and exposes task counts and the latest outcome as read-only state.

```cpp
class InboxViewModel : public std::enable_shared_from_this<InboxViewModel> {
public:
  InboxViewModel(UI::InvalidationScheduler invalidate)
      : screen(std::move(invalidate)) {}

  void Activate(UI::ViewModelTaskScope& owner) {
    scope = &owner;
    Load();
  }

  void Deactivate() {
    load.Cancel();
    scope = nullptr;
  }

  void Load() {
    screen.SetLoading();
    auto self = shared_from_this();
    load = scope->Start(
        [self](Async::TaskContext& context) {
          return self->LoadAsync(context);
        },
        [self](const UI::ViewModelTaskOutcome& result) {
          if (result.error) self->screen.SetError(*result.error);
        });
  }

  UI::AsyncPresentation<std::vector<Message>> screen;

private:
  Async::Task<void, UI::CommandError>
  LoadAsync(Async::TaskContext& context);

  UI::ViewModelTaskScope* scope{};
  UI::ViewModelTaskHandle load{};
};
```

Canceling a `ViewModelTaskHandle` stops one operation. `CancelAll()` stops every
operation and permanently closes the scope. Destroying the scope also cancels
all work, clears completion observers, and never waits for cleanup.

`Close(callback)` also stops new work and requests cancellation, but invokes
the callback only after every retained task has completed or observed
cancellation. Host integrations use this hook when a DI scope must outlive all
work started by its ViewModel.

The context passed to the scope should normally come from
`Application::CreateTaskContext(window)`. Window closure and application
shutdown then cancel the parent token even if application code forgets to
close the scope.

## Keyed lifetime host

`KeyedViewModelHost<T>` works with plain C++ types. No ViewModel base class is
required. A type may provide any of these hooks:

```cpp
void Activate(UI::ViewModelTaskScope& scope) noexcept;
Async::Task<void, UI::CommandError>
ActivateAsync(Async::TaskContext& context);
void Deactivate() noexcept;
Async::Task<void, UI::CommandError>
DeactivateAsync(Async::TaskContext& context);
```

The host has these exact rules:

- the first `Show(key)` calls the factory, creates an activation scope, calls
  `Activate`, and starts `ActivateAsync` once when present;
- `Show` with the current key reuses the same object and calls no hook;
- `Show` with another key calls `Deactivate`, cancels and releases the old
  activation scope, starts optional async cleanup, then creates the new object;
- `Hide()` performs the same release without creating a replacement;
- async cleanup runs in the host's observed cleanup scope; replacement does
  not wait for it;
- destroying the host cancels both active work and cleanup immediately, so
  slow cleanup cannot delay window or application shutdown.

Synchronous hooks are recognized only with the `noexcept` signatures above.
Work that can fail belongs in the async hooks, where domain errors and faults
are observed by the task scope. A thrown factory exception is returned as a
`UIError` from `Show()`.

The UI runtime does not guess application navigation policy. Connect lifetime
events explicitly:

- a retained keyed view calls `Show(key)` when its logical key changes and
  `Hide()` from its unmount cleanup;
- a tab calls `Hide()` when selection changes if inactive tabs should unload;
  keep the host mounted if tabs intentionally cache ViewModels;
- a popup calls `Hide()` when it closes;
- a window-owned host is canceled by both `Hide()` and the window task token;
- application shutdown cancels the application token and never waits for
  ViewModel cleanup.

This makes keyed reconciliation, tab caching, and popup caching choices
visible in application code instead of hiding them in the compositor.

Factories receive the key and an optional `ViewModelServiceResolver`. The
resolver is a non-owning function hook around `std::type_index`; it does not
store services and does not require a dependency-injection container.

That resolver remains the lightweight standalone factory path. Hosted Core
applications use `NGIN.UI.Hosting::HostedViewModelHost<T>` instead. It resolves
an owning `NGIN::Memory::Shared<T>` from the page scope and releases it only
after activation work and optional async deactivation have been observed. See
the [hosting package guide](../../Packages/NGIN.UI.Hosting/README.md) for the
complete scoped example.

## Compose async states synchronously

`AsyncPresentation<T>` exposes `Idle`, `Loading`, `Content`, `Empty`, and
`Error`, plus optional retry and cancel command bindings.

```cpp
const auto& state = viewModel.screen.Get();
switch (state.kind) {
case UI::AsyncPresentationKind::Loading:
  ComposeLoading(composer, viewModel.screen.CancelCommand());
  break;
case UI::AsyncPresentationKind::Content:
  ComposeMessages(composer, *state.content);
  break;
case UI::AsyncPresentationKind::Empty:
  ComposeEmpty(composer, viewModel.screen.RetryCommand());
  break;
case UI::AsyncPresentationKind::Error:
  ComposeError(composer, *state.error,
               viewModel.screen.RetryCommand());
  break;
case UI::AsyncPresentationKind::Idle:
  ComposeIdle(composer);
  break;
}
```

`Compose(Composer&)` remains an ordinary synchronous function. Tasks only
change observable ViewModel state; invalidation schedules composition of the
new snapshot. Never retain a `Composer` in a ViewModel, task, or lifecycle
hook.
