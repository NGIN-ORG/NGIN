# Derived state and validation

NGIN.UI keeps ViewModel state typed. A view can read derived values, but it
cannot write them, and dependencies are listed explicitly in C++.

## Read-only and computed values

Use `AsReadOnly()` when a View should observe a value without changing it.
Use `ComputedState<T>` when a value comes from other state:

```cpp
UI::State<Text::String> firstName{Text::String{"Ada"}};
UI::State<Text::String> lastName{Text::String{"Lovelace"}};

UI::ComputedState<Text::String> fullName{
    [&] {
      auto result = firstName.Get();
      result.Append(Text::String{" "});
      result.Append(lastName.Get());
      return result;
    },
    {UI::DependOn(firstName), UI::DependOn(lastName)}};

auto viewValue = fullName.AsReadOnly();
```

Dependencies are ordinary typed bindings. There is no global dependency
tracker, property-name string, or required reflection. A dependency cycle is
rejected by `SetDependencies()` and leaves the previous graph connected.

Use `StateBatch` for one logical update to several values. Values change
immediately; observer callbacks, computed values, and invalidation are
coalesced until the outer batch ends.

```cpp
{
  UI::StateBatch update;
  firstName.Set(Text::String{"Grace"});
  lastName.Set(Text::String{"Hopper"});
}
```

## Validate a field

`ValidationIssue` has a stable ID, field name, message, and severity. A field
may run ordered synchronous validators and one asynchronous validator.

```cpp
UI::ValidationField nameValidation{
    name.AsReadOnly(), UI::ValidationTrigger::Immediate};

nameValidation.AddSyncValidator([](const Text::String& value) {
  if (value.Empty()) {
    return std::vector{UI::ValidationIssue{{"name-required"}, {"Name"},
                                            {"Enter a name"}}};
  }
  return std::vector<UI::ValidationIssue>{};
});

nameValidation.SetAsyncValidator(
    application.CreateTaskContext(window),
    [](Async::TaskContext& context, Text::String value)
        -> Async::Task<std::vector<UI::ValidationIssue>,
                       UI::ValidationIssue> {
      auto issues = co_await CheckNameOnServer(context, std::move(value));
      co_return issues;
    });
```

Available triggers are:

- `Immediate`: validate each changed value;
- `Deferred`: validate only when `Validate()` is called;
- `Submit`: wait for the first `Validate()`, then follow later edits.

Starting a new async check cancels the previous check. A revision check also
prevents an older result from replacing results for newer input, even if the
underlying work finishes after cancellation. Destroying the field cancels its
active check and ignores late completion.

## Build a form and enable Save

`ValidationForm` keeps field order when it builds the summary. It exposes
read-only validity and pending state.

```cpp
UI::ValidationForm form{{nameValidation.AsBinding(),
                         passwordValidation.AsBinding()}};

UI::Command save{[&] { SaveModel(); }};
save.BindEnabled(form.IsValid());

// A submit or Check button can call this first.
form.ValidateAll();
```

`IsValid()` is false while any field has not yet been validated, is checking
asynchronously, or has an error-severity issue. Information and warning issues
remain in the summary but do not make the field invalid. Command binding uses
a weak subscription internally, so the derived value does not own the command
and no ownership cycle is created.

Views remain synchronous: read these bindings during `Compose(Composer&)` and
let normal state invalidation request the next composition pass. Never retain
the `Composer` in a validator or task.
