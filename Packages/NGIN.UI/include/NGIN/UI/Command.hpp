#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/Utilities/Callable.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <memory>
#include <optional>

namespace NGIN::UI {
/// @brief Stable category for an application-command failure.
enum class CommandErrorKind : UInt8 {
  Domain,
  Fault,
};

/// @brief Displayable, observable error produced by a command execution.
struct CommandError final {
  CommandErrorKind kind{CommandErrorKind::Domain};
  NGIN::Text::String code{};
  NGIN::Text::String message{};
  Int32 nativeCode{0};

  [[nodiscard]] auto operator==(const CommandError &) const noexcept
      -> bool = default;
};

/// @brief Expected-style result returned by synchronous commands.
template <typename T>
using CommandResult = NGIN::Utilities::Expected<T, CommandError>;

/// @brief Final state of the most recently observed command execution.
enum class CommandOutcomeKind : UInt8 {
  None,
  Succeeded,
  DomainError,
  Canceled,
  Fault,
};

/// @brief Observable result retained after a command finishes.
struct CommandOutcome final {
  CommandOutcomeKind kind{CommandOutcomeKind::None};
  std::optional<CommandError> error{};

  [[nodiscard]] auto operator==(const CommandOutcome &) const noexcept
      -> bool = default;
};

/// @brief Policy applied when an asynchronous command is invoked while busy.
enum class CommandConcurrencyPolicy : UInt8 {
  Reject,
  CancelPrevious,
  Queue,
};

/// @brief Immediate result of asking a command to execute.
enum class CommandInvocation : UInt8 {
  Started,
  Queued,
  Replaced,
  RejectedDisabled,
  RejectedRunning,
  RejectedQueueFull,
  RejectedExpired,
};

/// @brief Complete observable state of a command.
struct CommandStatus final {
  bool enabled{true};
  bool canExecute{true};
  bool isRunning{false};
  bool canCancel{false};
  UIntSize queuedCount{0};
  UInt64 executionId{0};
  CommandOutcome lastOutcome{};

  [[nodiscard]] auto operator==(const CommandStatus &) const noexcept
      -> bool = default;
};

/// @brief Copyable, lifetime-safe control-facing view of a command.
class CommandBinding final {
public:
  using ExecuteCallback = NGIN::Utilities::Callable<CommandInvocation()>;
  using CancelCallback = NGIN::Utilities::Callable<void()>;
  using StatusGetter = NGIN::Utilities::Callable<CommandStatus()>;
  using Subscriber =
      NGIN::Utilities::Callable<Subscription(StateObserver<CommandStatus>)>;

  CommandBinding() noexcept = default;
  CommandBinding(ExecuteCallback execute, CancelCallback cancel,
                 StatusGetter status, Subscriber subscriber = {});

  [[nodiscard]] auto Execute() const -> CommandInvocation;
  void Cancel() const noexcept;
  [[nodiscard]] auto Status() const -> CommandStatus;
  [[nodiscard]] auto CanExecute() const -> bool;
  [[nodiscard]] auto IsRunning() const -> bool;
  [[nodiscard]] auto Subscribe(StateObserver<CommandStatus> observer) const
      -> Subscription;
  [[nodiscard]] explicit operator bool() const noexcept;

private:
  ExecuteCallback m_execute{};
  CancelCallback m_cancel{};
  StatusGetter m_status{};
  Subscriber m_subscriber{};
};

/// @brief Synchronous action with observable availability and outcome.
class Command final {
public:
  using Action = NGIN::Utilities::Callable<CommandResult<void>()>;

  explicit Command(Action action, bool enabled = true,
                   InvalidationScheduler scheduler = {});
  explicit Command(NGIN::Utilities::Callable<void()> action,
                   bool enabled = true, InvalidationScheduler scheduler = {});
  Command(const Command &) = delete;
  Command(Command &&) noexcept = default;
  auto operator=(const Command &) -> Command & = delete;
  auto operator=(Command &&) noexcept -> Command & = delete;
  ~Command();

  [[nodiscard]] auto Execute() -> CommandInvocation;
  void SetEnabled(bool enabled);
  [[nodiscard]] auto Status() const -> const CommandStatus &;
  [[nodiscard]] auto Subscribe(StateObserver<CommandStatus> observer)
      -> Subscription;
  [[nodiscard]] auto AsBinding() const -> CommandBinding;

private:
  struct Storage;
  [[nodiscard]] static auto
  ExecuteStorage(const std::shared_ptr<Storage> &storage) -> CommandInvocation;
  std::shared_ptr<Storage> m_storage{};
};

/// @brief Coroutine action with observable lifetime, cancellation, and errors.
class AsyncCommand final {
public:
  using Action =
      NGIN::Utilities::Callable<NGIN::Async::Task<void, CommandError>(
          NGIN::Async::TaskContext &)>;

  AsyncCommand(
      NGIN::Async::TaskContext context, Action action, bool enabled = true,
      CommandConcurrencyPolicy concurrency = CommandConcurrencyPolicy::Reject,
      UIntSize queueCapacity = 1, InvalidationScheduler scheduler = {});
  AsyncCommand(const AsyncCommand &) = delete;
  AsyncCommand(AsyncCommand &&) noexcept = default;
  auto operator=(const AsyncCommand &) -> AsyncCommand & = delete;
  auto operator=(AsyncCommand &&) noexcept -> AsyncCommand & = delete;
  ~AsyncCommand();

  [[nodiscard]] auto Execute() -> CommandInvocation;
  void Cancel() noexcept;
  void SetEnabled(bool enabled);
  [[nodiscard]] auto Status() const -> const CommandStatus &;
  [[nodiscard]] auto Subscribe(StateObserver<CommandStatus> observer)
      -> Subscription;
  [[nodiscard]] auto AsBinding() const -> CommandBinding;

private:
  struct Storage;
  struct Run;
  [[nodiscard]] static auto
  ExecuteStorage(const std::shared_ptr<Storage> &storage) -> CommandInvocation;
  static void CancelStorage(const std::shared_ptr<Storage> &storage) noexcept;
  static void StartRun(const std::shared_ptr<Storage> &storage);
  static void FinishRun(const std::shared_ptr<Storage> &storage,
                        const std::shared_ptr<Run> &run,
                        CommandOutcome outcome);
  [[nodiscard]] static auto RunAction(std::shared_ptr<Storage> storage,
                                      std::shared_ptr<Run> run)
      -> NGIN::Async::Task<void>;
  std::shared_ptr<Storage> m_storage{};
};
} // namespace NGIN::UI
