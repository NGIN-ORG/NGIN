# Build an NGIN.UI app with MVVM

MVVM in NGIN.UI is a way to separate screen data from screen layout. It does
not require a base class, reflection, string property names, or generated code.

## What owns what

| Part | Responsibility |
|---|---|
| Model | Application data and business rules. It is ordinary C++. |
| View | The controls, layout, labels, and visual states for one screen. |
| ViewModel | Screen state, validation, commands, and owned asynchronous work. |
| Composer | A short-lived builder used by the View during one synchronous pass. |
| `State<T>` | A writable value owned by the ViewModel. |
| `ReadOnlyBinding<T>` | A value the View may observe but not change. |
| `Binding<T>` | Typed two-way access for an editable control. |
| `Command` | A synchronous user action and its availability or result. |
| `AsyncCommand` | An awaited action with progress, cancellation, and errors. |
| `ViewModelTaskScope` | The lifetime owner for loads and other screen work. |

The View owns the controls and layouts. The ViewModel never stores a
`Composer`, control node, or native window handle. A task changes observable
state; the next normal composition pass displays it.

## A practical file layout

```text
Notes/
  Models/Note.hpp
  ViewModels/EditorViewModel.hpp
  ViewModels/EditorViewModel.cpp
  Views/EditorView.hpp
  Views/EditorView.cpp
  App.cpp
```

Small apps may keep these in fewer files. The ownership rules matter more than
the folders.

## State, derived state, and a synchronous command

```cpp
class CounterViewModel final {
public:
  CounterViewModel()
      : doubled([this] { return count.Get() * 2; },
                {NGIN::UI::DependOn(count)}),
        increment([this] {
          static_cast<void>(count.Set(count.Get() + 1));
        }) {}

  NGIN::UI::State<int> count{0};
  NGIN::UI::ComputedState<int> doubled;
  NGIN::UI::Command increment;
};
```

Expose `Bind(count)` only when the View should edit the value. Prefer
`Observe(count)` or `doubled.AsReadOnly()` for display-only values.

## Validation and Save

Create fields from read-only state, add typed validators, then let form
validity control Save:

```cpp
nameField = std::make_unique<NGIN::UI::ValidationField<NGIN::Text::String>>(
    NGIN::UI::Observe(name), NGIN::UI::ValidationTrigger::Immediate);
nameField->AddSyncValidator([](const NGIN::Text::String& value) {
  if (!value.Empty()) return std::vector<NGIN::UI::ValidationIssue>{};
  return std::vector{NGIN::UI::ValidationIssue{
      .id = NGIN::Text::String{"name-required"},
      .field = NGIN::Text::String{"Name"},
      .message = NGIN::Text::String{"Enter a name"}}};
});

form = std::make_unique<NGIN::UI::ValidationForm>(
    std::vector{nameField->AsBinding()});
save->BindEnabled(form->IsValid());
```

Call `form->ValidateAll()` when the user submits. `ValidationForm::Summary()`
provides the ordered issues to display. Asynchronous validators use a
`TaskContext`; stale results are discarded when the input changes.

## Async Save, cancellation, and errors

```cpp
save = std::make_unique<NGIN::UI::AsyncCommand>(
    application.CreateTaskContext(window),
    [this](NGIN::Async::TaskContext& context) ->
        NGIN::Async::Task<void, NGIN::UI::CommandError> {
      co_await context.YieldNow();
      if (!StoreDocument()) {
        co_await NGIN::Async::DomainFailure(NGIN::UI::CommandError{
            .code = NGIN::Text::String{"save-rejected"},
            .message =
                NGIN::Text::String{"The document could not be saved"}});
      }
    });
```

Bind a button to `save->AsBinding()`. Read `save->Status().isRunning` to show
progress and call `save->Cancel()` from a Cancel button. The last outcome is
`Succeeded`, `DomainError`, `Canceled`, or `Fault`; errors remain observable
until another execution finishes. The default reject policy prevents a double
click from starting a second save.

## Loading, empty, content, retry, and error

Use `ViewModelTaskScope` for work started because a screen is mounted. Put its
visible result in `AsyncPresentation<T>`:

```cpp
void InboxViewModel::Activate(NGIN::UI::ViewModelTaskScope& owner) noexcept {
  scope = &owner;
  screen.SetLoading();
  load = scope->Start(
      [this](NGIN::Async::TaskContext& context) { return LoadAsync(context); },
      [this](const NGIN::UI::ViewModelTaskOutcome& result) {
        if (result.error) screen.SetError(*result.error);
      });
}

void InboxViewModel::Deactivate() noexcept {
  load.Cancel();
  scope = nullptr;
}
```

The synchronous View switches on `screen.Get().kind` and shows loading,
content, empty, or error controls. Give `AsyncPresentation` retry and cancel
command bindings so the same actions work from buttons and accessibility.

For keyed screens, `KeyedViewModelHost<T>` creates and activates a ViewModel,
reuses the same key, and cancels it on replacement or removal. Window closure
and application shutdown also cancel contexts created for that window.

## Composition

```cpp
void ComposeEditor(NGIN::UI::Composer& composer,
                   NGIN::UI::NativeTextSystem& text,
                   EditorViewModel& viewModel) {
  // Declare the screen's controls and layout here.
  // Read state and command status; never await or retain composer.
}
```

The standalone, hosted, and headless Gallery products all call the same
`ComposeMainView` with the same `GalleryViewModel`. See
[`Gallery.hpp`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/include/NGIN/UIGallery/Gallery.hpp)
and [`Gallery.cpp`](https://github.com/NGIN-ORG/NGIN/tree/main/Examples/NGIN.UI.Gallery/src/Gallery.cpp) for the
complete implementation.

NGIN.UI 0.3 ships the manual typed APIs only. Source generation was evaluated
and deliberately remains optional future work: a generator must emit these
same public contracts, add no runtime requirement, and be removable without
changing application behavior.

More focused guides cover [commands](ngin-ui-mvvm-commands.md),
[state and validation](ngin-ui-state-validation.md), and
[ViewModel task lifetime](ngin-ui-viewmodel-lifetime.md).
