#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Invalidation.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <algorithm>
#include <concepts>
#include <memory>
#include <utility>
#include <vector>

namespace NGIN::UI {
/// @brief Move-only lifetime token for an observable-state subscription.
class Subscription final {
public:
  Subscription() noexcept = default;

  explicit Subscription(NGIN::Utilities::Callable<void()> cancel)
      : m_cancel(std::move(cancel)) {}

  Subscription(const Subscription &) = delete;
  Subscription(Subscription &&other) noexcept
      : m_cancel(std::move(other.m_cancel)) {}

  auto operator=(const Subscription &) -> Subscription & = delete;
  auto operator=(Subscription &&other) noexcept -> Subscription & {
    if (this != &other) {
      Cancel();
      m_cancel = std::move(other.m_cancel);
    }
    return *this;
  }

  ~Subscription() { Cancel(); }

  void Cancel() noexcept {
    if (m_cancel) {
      auto cancel = std::move(m_cancel);
      cancel();
    }
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(m_cancel);
  }

private:
  NGIN::Utilities::Callable<void()> m_cancel{};
};

/// @brief Callback invoked after an observable state value changes.
template <typename T>
using StateObserver = NGIN::Utilities::Callable<void(const T &)>;

/// @brief Callback used by state to schedule one or more pipeline stages.
using InvalidationScheduler = NGIN::Utilities::Callable<void(InvalidationKind)>;

/// @brief UI-thread-owned observable value with validation and invalidation.
template <typename T> class State final {
public:
  explicit State(T initialValue = {}, InvalidationScheduler scheduler = {},
                 const InvalidationKind invalidation =
                     InvalidationKind::Compose | InvalidationKind::Paint)
      : m_storage(std::make_shared<Storage>(
            std::move(initialValue), std::move(scheduler), invalidation)) {}

  State(const State &) = delete;
  State(State &&) = delete;
  auto operator=(const State &) -> State & = delete;
  auto operator=(State &&) -> State & = delete;
  ~State() = default;

  [[nodiscard]] auto Get() const noexcept -> const T & {
    return m_storage->value;
  }

  [[nodiscard]] auto Set(T value) -> bool {
    if constexpr (std::equality_comparable<T>) {
      if (m_storage->value == value) {
        return false;
      }
    }

    m_storage->value = std::move(value);
    Notify();
    if (m_storage->scheduleInvalidation) {
      m_storage->scheduleInvalidation(m_storage->invalidation);
    }
    return true;
  }

  template <typename UpdateValue>
  [[nodiscard]] auto Update(UpdateValue &&updateValue) -> bool {
    auto next = m_storage->value;
    std::forward<UpdateValue>(updateValue)(next);
    return Set(std::move(next));
  }

  [[nodiscard]] auto Subscribe(StateObserver<T> observer) -> Subscription {
    const auto id = m_storage->nextSubscriberId++;
    m_storage->subscribers.push_back(Subscriber{
        .id = id,
        .observer = std::make_shared<StateObserver<T>>(std::move(observer)),
    });
    std::weak_ptr<Storage> storage = m_storage;
    return Subscription{[storage = std::move(storage), id] {
      if (const auto locked = storage.lock()) {
        std::erase_if(locked->subscribers, [id](const Subscriber &subscriber) {
          return subscriber.id == id;
        });
      }
    }};
  }

private:
  struct Subscriber final {
    UInt64 id{0};
    std::shared_ptr<StateObserver<T>> observer{};
  };

  struct Storage final {
    Storage(T initialValue, InvalidationScheduler scheduler,
            const InvalidationKind invalidationKind)
        : value(std::move(initialValue)),
          scheduleInvalidation(std::move(scheduler)),
          invalidation(invalidationKind) {}

    T value;
    InvalidationScheduler scheduleInvalidation{};
    InvalidationKind invalidation{InvalidationKind::None};
    UInt64 nextSubscriberId{1};
    std::vector<Subscriber> subscribers{};
  };

  void Notify() {
    std::vector<std::shared_ptr<StateObserver<T>>> observers;
    observers.reserve(m_storage->subscribers.size());
    for (const auto &subscriber : m_storage->subscribers) {
      observers.push_back(subscriber.observer);
    }
    for (const auto &observer : observers) {
      if (*observer) {
        (*observer)(m_storage->value);
      }
    }
  }

  std::shared_ptr<Storage> m_storage;
};

/// @brief Copyable read/write view over observable state or custom accessors.
template <typename T> class Binding final {
public:
  using Getter = NGIN::Utilities::Callable<const T &()>;
  using Setter = NGIN::Utilities::Callable<UIResult<void>(T)>;
  using Subscriber = NGIN::Utilities::Callable<Subscription(StateObserver<T>)>;
  using Validator = NGIN::Utilities::Callable<UIResult<void>(const T &)>;

  Binding() noexcept = default;

  Binding(Getter getter, Setter setter, Subscriber subscriber = {})
      : m_getter(std::move(getter)), m_setter(std::move(setter)),
        m_subscriber(std::move(subscriber)) {}

  [[nodiscard]] auto Get() const -> const T & { return m_getter(); }

  [[nodiscard]] auto Set(T value) const -> UIResult<void> {
    if (!m_setter) {
      return MakeUIError(UIErrorCode::InvalidState, "Binding is not writable",
                         "NGIN.UI", "Binding::Set");
    }
    return m_setter(std::move(value));
  }

  [[nodiscard]] auto Subscribe(StateObserver<T> observer) const
      -> Subscription {
    return m_subscriber ? m_subscriber(std::move(observer)) : Subscription{};
  }

  [[nodiscard]] auto WithValidation(Validator validator) const -> Binding {
    auto setter = m_setter;
    return Binding{
        m_getter,
        [setter = std::move(setter),
         validator = std::move(validator)](T value) mutable -> UIResult<void> {
          auto valid = validator(value);
          if (!valid) {
            return std::move(valid).Error();
          }
          if (!setter) {
            return MakeUIError(UIErrorCode::InvalidState,
                               "Binding is not writable", "NGIN.UI",
                               "Binding::Set");
          }
          return setter(std::move(value));
        },
        m_subscriber,
    };
  }

  [[nodiscard]] auto IsReadable() const noexcept -> bool {
    return static_cast<bool>(m_getter);
  }

  [[nodiscard]] auto IsWritable() const noexcept -> bool {
    return static_cast<bool>(m_setter);
  }

  [[nodiscard]] explicit operator bool() const noexcept { return IsReadable(); }

private:
  Getter m_getter{};
  Setter m_setter{};
  Subscriber m_subscriber{};
};

template <typename T> [[nodiscard]] auto Bind(State<T> &state) -> Binding<T> {
  return Binding<T>{
      [&state]() -> const T & { return state.Get(); },
      [&state](T value) -> UIResult<void> {
        static_cast<void>(state.Set(std::move(value)));
        return {};
      },
      [&state](StateObserver<T> observer) {
        return state.Subscribe(std::move(observer));
      },
  };
}
} // namespace NGIN::UI
