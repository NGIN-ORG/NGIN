#pragma once

#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/State.hpp>

#include <optional>
#include <utility>

namespace NGIN::UI {
/// @brief Visible state of asynchronous ViewModel content.
enum class AsyncPresentationKind : UInt8 {
  Idle,
  Loading,
  Content,
  Empty,
  Error,
};

/// @brief Typed snapshot consumed synchronously by a View.
template <typename T> struct AsyncPresentationSnapshot final {
  AsyncPresentationKind kind{AsyncPresentationKind::Idle};
  std::optional<T> content{};
  std::optional<CommandError> error{};
};

/// @brief Observable idle/loading/content/empty/error presentation state.
template <typename T> class AsyncPresentation final {
public:
  explicit AsyncPresentation(InvalidationScheduler scheduler = {})
      : m_state({}, std::move(scheduler)) {}

  AsyncPresentation(const AsyncPresentation &) = delete;
  AsyncPresentation(AsyncPresentation &&) = delete;
  auto operator=(const AsyncPresentation &) -> AsyncPresentation & = delete;
  auto operator=(AsyncPresentation &&) -> AsyncPresentation & = delete;
  ~AsyncPresentation() = default;

  void SetIdle() { Set(AsyncPresentationKind::Idle); }
  void SetLoading() { Set(AsyncPresentationKind::Loading); }
  void SetContent(T content) {
    static_cast<void>(m_state.Set(AsyncPresentationSnapshot<T>{
        .kind = AsyncPresentationKind::Content,
        .content = std::move(content),
    }));
  }
  void SetEmpty() { Set(AsyncPresentationKind::Empty); }
  void SetError(CommandError error) {
    static_cast<void>(m_state.Set(AsyncPresentationSnapshot<T>{
        .kind = AsyncPresentationKind::Error,
        .error = std::move(error),
    }));
  }
  [[nodiscard]] auto Get() const noexcept
      -> const AsyncPresentationSnapshot<T> & {
    return m_state.Get();
  }
  [[nodiscard]] auto AsReadOnly() const
      -> ReadOnlyBinding<AsyncPresentationSnapshot<T>> {
    return m_state.AsReadOnly();
  }
  void SetRetryCommand(CommandBinding retry) { m_retry = std::move(retry); }
  void SetCancelCommand(CommandBinding cancel) { m_cancel = std::move(cancel); }
  [[nodiscard]] auto RetryCommand() const -> const CommandBinding & {
    return m_retry;
  }
  [[nodiscard]] auto CancelCommand() const -> const CommandBinding & {
    return m_cancel;
  }

private:
  void Set(const AsyncPresentationKind kind) {
    static_cast<void>(m_state.Set(AsyncPresentationSnapshot<T>{.kind = kind}));
  }

  State<AsyncPresentationSnapshot<T>> m_state;
  CommandBinding m_retry{};
  CommandBinding m_cancel{};
};
} // namespace NGIN::UI
