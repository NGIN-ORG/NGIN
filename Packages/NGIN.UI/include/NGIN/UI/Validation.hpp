#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace NGIN::UI {
/// @brief Importance of a validation issue presented to a user.
enum class ValidationSeverity : UInt8 {
  Info,
  Warning,
  Error,
};

/// @brief Time at which a field automatically validates changed input.
enum class ValidationTrigger : UInt8 {
  Immediate,
  Deferred,
  Submit,
};

/// @brief Stable, displayable validation result for one field.
struct ValidationIssue final {
  NGIN::Text::String id{};
  NGIN::Text::String field{};
  NGIN::Text::String message{};
  ValidationSeverity severity{ValidationSeverity::Error};

  [[nodiscard]] auto operator==(const ValidationIssue &) const noexcept
      -> bool = default;
};

/// @brief Type-erased field state used to build a validation form.
class ValidationFieldBinding final {
public:
  using Action = NGIN::Utilities::Callable<void()>;

  ValidationFieldBinding() noexcept = default;
  ValidationFieldBinding(ReadOnlyBinding<std::vector<ValidationIssue>> issues,
                         ReadOnlyBinding<bool> valid,
                         ReadOnlyBinding<bool> validating, Action validate,
                         Action cancel)
      : m_issues(std::move(issues)), m_valid(std::move(valid)),
        m_validating(std::move(validating)), m_validate(std::move(validate)),
        m_cancel(std::move(cancel)) {}

  [[nodiscard]] auto Issues() const
      -> const ReadOnlyBinding<std::vector<ValidationIssue>> & {
    return m_issues;
  }
  [[nodiscard]] auto IsValid() const -> const ReadOnlyBinding<bool> & {
    return m_valid;
  }
  [[nodiscard]] auto IsValidating() const -> const ReadOnlyBinding<bool> & {
    return m_validating;
  }
  void Validate() const {
    if (m_validate) {
      m_validate();
    }
  }
  void Cancel() const noexcept {
    if (m_cancel) {
      m_cancel();
    }
  }

private:
  ReadOnlyBinding<std::vector<ValidationIssue>> m_issues{};
  ReadOnlyBinding<bool> m_valid{};
  ReadOnlyBinding<bool> m_validating{};
  Action m_validate{};
  Action m_cancel{};
};

/// @brief Typed synchronous and asynchronous validation for one observable.
template <typename T> class ValidationField final {
public:
  using SyncValidator =
      NGIN::Utilities::Callable<std::vector<ValidationIssue>(const T &value)>;
  using AsyncValidator = NGIN::Utilities::Callable<
      NGIN::Async::Task<std::vector<ValidationIssue>, ValidationIssue>(
          NGIN::Async::TaskContext &context, T value)>;

  explicit ValidationField(
      ReadOnlyBinding<T> input,
      ValidationTrigger trigger = ValidationTrigger::Immediate,
      InvalidationScheduler scheduler = {})
      : m_storage(
            Storage::Create(std::move(input), trigger, std::move(scheduler))) {}

  ValidationField(const ValidationField &) = delete;
  ValidationField(ValidationField &&) noexcept = default;
  auto operator=(const ValidationField &) -> ValidationField & = delete;
  auto operator=(ValidationField &&) noexcept -> ValidationField & = delete;
  ~ValidationField() { Stop(); }

  /// @brief Adds a validator. Results retain validator and issue order.
  void AddSyncValidator(SyncValidator validator) {
    if (!m_storage || !m_storage->alive) {
      return;
    }
    m_storage->syncValidators.push_back(std::move(validator));
    if (m_storage->trigger == ValidationTrigger::Immediate ||
        m_storage->submitted) {
      ValidateStorage(m_storage);
    }
  }

  /// @brief Configures one cancellation-aware asynchronous validator.
  void SetAsyncValidator(NGIN::Async::TaskContext context,
                         AsyncValidator validator) {
    if (!m_storage || !m_storage->alive) {
      return;
    }
    m_storage->context = std::move(context);
    m_storage->asyncValidator = std::move(validator);
    if (m_storage->trigger == ValidationTrigger::Immediate ||
        m_storage->submitted) {
      ValidateStorage(m_storage);
    }
  }

  /// @brief Validates the latest value and marks submit-triggered fields used.
  void Validate() { ValidateStorage(m_storage); }

  /// @brief Cancels pending validation without publishing a cancellation issue.
  void Cancel() noexcept { CancelStorage(m_storage); }

  [[nodiscard]] auto Issues() const
      -> ReadOnlyBinding<std::vector<ValidationIssue>> {
    return m_storage->issues.AsReadOnly();
  }
  [[nodiscard]] auto IsValid() const -> ReadOnlyBinding<bool> {
    return m_storage->valid->AsReadOnly();
  }
  [[nodiscard]] auto IsValidating() const -> ReadOnlyBinding<bool> {
    return m_storage->validating.AsReadOnly();
  }
  [[nodiscard]] auto AsBinding() const -> ValidationFieldBinding {
    const auto weak = std::weak_ptr<Storage>{m_storage};
    return ValidationFieldBinding{
        Issues(),
        IsValid(),
        IsValidating(),
        [weak] {
          if (const auto storage = weak.lock()) {
            ValidateStorage(storage);
          }
        },
        [weak] {
          if (const auto storage = weak.lock()) {
            CancelStorage(storage);
          }
        },
    };
  }

private:
  struct Run final {
    Run(const UInt64 runVersion, NGIN::Async::TaskContext runContext)
        : version(runVersion), context(std::move(runContext)) {}

    UInt64 version{0};
    NGIN::Async::CancellationSource cancellation{};
    NGIN::Async::TaskContext context;
  };

  struct Storage final : std::enable_shared_from_this<Storage> {
    Storage(ReadOnlyBinding<T> source, const ValidationTrigger when,
            InvalidationScheduler scheduler)
        : input(std::move(source)), trigger(when), issues({}, scheduler),
          validating(false, scheduler), validated(false, std::move(scheduler)) {
    }

    [[nodiscard]] static auto Create(ReadOnlyBinding<T> source,
                                     const ValidationTrigger trigger,
                                     InvalidationScheduler scheduler)
        -> std::shared_ptr<Storage> {
      auto storage = std::shared_ptr<Storage>(
          new Storage(std::move(source), trigger, std::move(scheduler)));
      const auto weak = std::weak_ptr<Storage>{storage};
      storage->valid = std::make_unique<ComputedState<bool>>(
          [weak] {
            const auto current = weak.lock();
            if (!current || !current->validated.Get() ||
                current->validating.Get()) {
              return false;
            }
            for (const auto &issue : current->issues.Get()) {
              if (issue.severity == ValidationSeverity::Error) {
                return false;
              }
            }
            return true;
          },
          std::vector<ObservableDependency>{DependOn(storage->issues),
                                            DependOn(storage->validating),
                                            DependOn(storage->validated)});
      storage->inputSubscription = storage->input.Subscribe([weak](const T &) {
        if (const auto current = weak.lock()) {
          InputChanged(current);
        }
      });
      if (trigger == ValidationTrigger::Immediate) {
        ValidateStorage(storage);
      }
      return storage;
    }

    ReadOnlyBinding<T> input{};
    ValidationTrigger trigger{ValidationTrigger::Immediate};
    std::vector<SyncValidator> syncValidators{};
    NGIN::Async::TaskContext context{NGIN::Execution::ExecutorRef{}};
    AsyncValidator asyncValidator{};
    State<std::vector<ValidationIssue>> issues;
    State<bool> validating;
    State<bool> validated;
    std::unique_ptr<ComputedState<bool>> valid{};
    Subscription inputSubscription{};
    std::shared_ptr<Run> active{};
    UInt64 version{0};
    bool submitted{false};
    bool alive{true};
  };

  [[nodiscard]] static auto FaultIssue(const NGIN::Async::AsyncFault &fault)
      -> ValidationIssue {
    return ValidationIssue{
        .id = NGIN::Text::String{"async-validation-fault"},
        .message = NGIN::Text::String{fault.message.empty()
                                          ? "Validation could not finish"
                                          : fault.message},
        .severity = ValidationSeverity::Error,
    };
  }

  [[nodiscard]] static auto ExceptionIssue() -> ValidationIssue {
    return ValidationIssue{
        .id = NGIN::Text::String{"validation-exception"},
        .message = NGIN::Text::String{"Validation threw an exception"},
        .severity = ValidationSeverity::Error,
    };
  }

  static void InputChanged(const std::shared_ptr<Storage> &storage) {
    if (!storage->alive) {
      return;
    }
    if (storage->trigger == ValidationTrigger::Immediate ||
        storage->submitted) {
      ValidateStorage(storage);
      return;
    }
    CancelStorage(storage);
    StateBatch batch;
    static_cast<void>(storage->issues.Set({}));
    static_cast<void>(storage->validated.Set(false));
  }

  static void ValidateStorage(const std::shared_ptr<Storage> &storage) {
    if (!storage || !storage->alive || !storage->input) {
      return;
    }
    storage->submitted =
        storage->submitted || storage->trigger == ValidationTrigger::Submit;
    if (storage->active) {
      storage->active->cancellation.Cancel();
      storage->active.reset();
    }
    const auto version = ++storage->version;
    auto syncIssues = std::vector<ValidationIssue>{};
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      for (auto &validator : storage->syncValidators) {
        auto next = validator(storage->input.Get());
        syncIssues.insert(syncIssues.end(),
                          std::make_move_iterator(next.begin()),
                          std::make_move_iterator(next.end()));
      }
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      syncIssues.push_back(ExceptionIssue());
    }
#endif

    {
      StateBatch batch;
      static_cast<void>(storage->issues.Set(syncIssues));
      static_cast<void>(storage->validated.Set(true));
      static_cast<void>(
          storage->validating.Set(static_cast<bool>(storage->asyncValidator)));
    }
    if (!storage->asyncValidator) {
      return;
    }

    auto run = std::make_shared<Run>(version, storage->context);
    run->context.BindLinkedCancellationToken(run->cancellation.GetToken());
    storage->active = run;
    auto value = storage->input.Get();
    NGIN::Async::Detach(run->context, RunAsync(storage, run, std::move(value),
                                               std::move(syncIssues)));
  }

  [[nodiscard]] static auto RunAsync(std::shared_ptr<Storage> storage,
                                     std::shared_ptr<Run> run, T value,
                                     std::vector<ValidationIssue> syncIssues)
      -> NGIN::Async::Task<void> {
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      auto operation = NGIN::Async::Spawn(
          run->context,
          storage->asyncValidator(run->context, std::move(value)));
      auto completion = co_await operation;
      if (!storage->alive || !storage->active ||
          storage->active->version != run->version ||
          storage->version != run->version) {
        co_return;
      }

      if (completion.Succeeded()) {
        auto next = std::move(completion).Value();
        syncIssues.insert(syncIssues.end(),
                          std::make_move_iterator(next.begin()),
                          std::make_move_iterator(next.end()));
      } else if (completion.IsDomainError()) {
        syncIssues.push_back(std::move(completion).DomainError());
      } else if (completion.IsFault()) {
        syncIssues.push_back(FaultIssue(completion.Fault()));
      }
      FinishAsync(storage, run, std::move(syncIssues));
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      if (storage->alive && storage->active &&
          storage->active->version == run->version &&
          storage->version == run->version) {
        syncIssues.push_back(ExceptionIssue());
        FinishAsync(storage, run, std::move(syncIssues));
      }
    }
#endif
  }

  static void FinishAsync(const std::shared_ptr<Storage> &storage,
                          const std::shared_ptr<Run> &run,
                          std::vector<ValidationIssue> issues) {
    if (!storage->alive || !storage->active ||
        storage->active->version != run->version ||
        storage->version != run->version) {
      return;
    }
    storage->active.reset();
    StateBatch batch;
    static_cast<void>(storage->issues.Set(std::move(issues)));
    static_cast<void>(storage->validating.Set(false));
  }

  static void CancelStorage(const std::shared_ptr<Storage> &storage) noexcept {
    if (!storage || !storage->alive) {
      return;
    }
    ++storage->version;
    const auto wasActive = static_cast<bool>(storage->active);
    if (storage->active) {
      storage->active->cancellation.Cancel();
      storage->active.reset();
    }
    StateBatch batch;
    static_cast<void>(storage->validating.Set(false));
    if (wasActive) {
      static_cast<void>(storage->validated.Set(false));
    }
  }

  void Stop() noexcept {
    if (!m_storage) {
      return;
    }
    CancelStorage(m_storage);
    m_storage->alive = false;
    m_storage->inputSubscription.Cancel();
    m_storage->asyncValidator = nullptr;
    m_storage->syncValidators.clear();
  }

  std::shared_ptr<Storage> m_storage{};
};

/// @brief Aggregates ordered field issues, validity, and pending state.
class ValidationForm final {
public:
  explicit ValidationForm(std::vector<ValidationFieldBinding> fields)
      : m_storage(std::make_shared<Storage>(std::move(fields))),
        m_summary(
            [storage = m_storage] {
              auto result = std::vector<ValidationIssue>{};
              for (const auto &field : storage->fields) {
                const auto &issues = field.Issues().Get();
                result.insert(result.end(), issues.begin(), issues.end());
              }
              return result;
            },
            SummaryDependencies(m_storage->fields)),
        m_validating(
            [storage = m_storage] {
              for (const auto &field : storage->fields) {
                if (field.IsValidating().Get()) {
                  return true;
                }
              }
              return false;
            },
            ValidatingDependencies(m_storage->fields)),
        m_valid(
            [storage = m_storage] {
              for (const auto &field : storage->fields) {
                if (!field.IsValid().Get()) {
                  return false;
                }
              }
              return true;
            },
            ValidDependencies(m_storage->fields)) {}

  ValidationForm(const ValidationForm &) = delete;
  ValidationForm(ValidationForm &&) = delete;
  auto operator=(const ValidationForm &) -> ValidationForm & = delete;
  auto operator=(ValidationForm &&) -> ValidationForm & = delete;
  ~ValidationForm() = default;

  void ValidateAll() const {
    StateBatch batch;
    for (const auto &field : m_storage->fields) {
      field.Validate();
    }
  }
  void Cancel() const noexcept {
    for (const auto &field : m_storage->fields) {
      field.Cancel();
    }
  }
  [[nodiscard]] auto Summary()
      -> ReadOnlyBinding<std::vector<ValidationIssue>> {
    return m_summary.AsReadOnly();
  }
  [[nodiscard]] auto IsValidating() -> ReadOnlyBinding<bool> {
    return m_validating.AsReadOnly();
  }
  [[nodiscard]] auto IsValid() -> ReadOnlyBinding<bool> {
    return m_valid.AsReadOnly();
  }

private:
  struct Storage final {
    explicit Storage(std::vector<ValidationFieldBinding> validationFields)
        : fields(std::move(validationFields)) {}
    std::vector<ValidationFieldBinding> fields{};
  };

  [[nodiscard]] static auto
  SummaryDependencies(const std::vector<ValidationFieldBinding> &fields)
      -> std::vector<ObservableDependency> {
    auto dependencies = std::vector<ObservableDependency>{};
    dependencies.reserve(fields.size());
    for (const auto &field : fields) {
      dependencies.push_back(DependOn(field.Issues()));
    }
    return dependencies;
  }
  [[nodiscard]] static auto
  ValidatingDependencies(const std::vector<ValidationFieldBinding> &fields)
      -> std::vector<ObservableDependency> {
    auto dependencies = std::vector<ObservableDependency>{};
    dependencies.reserve(fields.size());
    for (const auto &field : fields) {
      dependencies.push_back(DependOn(field.IsValidating()));
    }
    return dependencies;
  }
  [[nodiscard]] static auto
  ValidDependencies(const std::vector<ValidationFieldBinding> &fields)
      -> std::vector<ObservableDependency> {
    auto dependencies = std::vector<ObservableDependency>{};
    dependencies.reserve(fields.size());
    for (const auto &field : fields) {
      dependencies.push_back(DependOn(field.IsValid()));
    }
    return dependencies;
  }

  std::shared_ptr<Storage> m_storage{};
  ComputedState<std::vector<ValidationIssue>> m_summary;
  ComputedState<bool> m_validating;
  ComputedState<bool> m_valid;
};
} // namespace NGIN::UI
