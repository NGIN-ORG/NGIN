#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Invalidation.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <algorithm>
#include <concepts>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace NGIN::UI {
template <typename T> class ReadOnlyBinding;
template <typename T> class Binding;

/// @brief Process-wide counters for observable subscription lifetime checks.
struct SubscriptionDiagnostics final {
  UInt64 activeCount{0};
  UInt64 peakActiveCount{0};
  UInt64 createdCount{0};
  UInt64 canceledCount{0};

  [[nodiscard]] auto operator==(const SubscriptionDiagnostics &) const noexcept
      -> bool = default;
};

/// @brief Returns a snapshot used to detect leaked subscriptions.
[[nodiscard]] auto CurrentSubscriptionDiagnostics() noexcept
    -> SubscriptionDiagnostics;

namespace Detail {
/// @brief Internal identity node used to validate explicit observable graphs.
struct ObservableNode final {
  UInt64 id{0};
  std::vector<std::weak_ptr<ObservableNode>> dependencies{};
};

[[nodiscard]] auto IsCollectingReads() noexcept -> bool;
/// @brief Collector for one currently executing component.
class ReadCollection;
/// @brief Temporarily isolates the implementation of an explicitly observable
/// binding.
class SuspendReadCollection final {
public:
  SuspendReadCollection() noexcept;
  ~SuspendReadCollection();
  SuspendReadCollection(const SuspendReadCollection &) = delete;
  auto operator=(const SuspendReadCollection &)
      -> SuspendReadCollection & = delete;

private:
  ReadCollection *m_previous;
};
template <typename Read> void TrackRead(Read &&read) noexcept;

[[nodiscard]] auto CreateObservableNode() -> std::shared_ptr<ObservableNode>;
[[nodiscard]] auto DependsOn(const std::shared_ptr<ObservableNode> &start,
                             const std::shared_ptr<ObservableNode> &target)
    -> bool;
void DispatchObservable(UInt64 identity,
                        NGIN::Utilities::Callable<void()> publish);
void BeginStateBatch() noexcept;
void EndStateBatch();
void RegisterSubscription() noexcept;
void UnregisterSubscription() noexcept;
} // namespace Detail

/// @brief Batches observable notifications until the outer scope exits.
class StateBatch final {
public:
  StateBatch() noexcept;
  StateBatch(const StateBatch &) = delete;
  StateBatch(StateBatch &&) = delete;
  auto operator=(const StateBatch &) -> StateBatch & = delete;
  auto operator=(StateBatch &&) -> StateBatch & = delete;
  ~StateBatch() noexcept(false);
};

/// @brief Move-only lifetime token for an observable-state subscription.
class Subscription final {
public:
  Subscription() noexcept = default;

  explicit Subscription(NGIN::Utilities::Callable<void()> cancel)
      : m_cancel(std::move(cancel)), m_registered(static_cast<bool>(m_cancel)) {
    if (m_registered) {
      Detail::RegisterSubscription();
    }
  }

  Subscription(const Subscription &) = delete;
  Subscription(Subscription &&other) noexcept
      : m_cancel(std::move(other.m_cancel)),
        m_registered(std::exchange(other.m_registered, false)) {}

  auto operator=(const Subscription &) -> Subscription & = delete;
  auto operator=(Subscription &&other) noexcept -> Subscription & {
    if (this != &other) {
      Cancel();
      m_cancel = std::move(other.m_cancel);
      m_registered = std::exchange(other.m_registered, false);
    }
    return *this;
  }

  ~Subscription() { Cancel(); }

  void Cancel() noexcept {
    if (m_cancel) {
      auto cancel = std::move(m_cancel);
      cancel();
    }
    if (std::exchange(m_registered, false)) {
      Detail::UnregisterSubscription();
    }
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(m_cancel);
  }

private:
  NGIN::Utilities::Callable<void()> m_cancel{};
  bool m_registered{false};
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
  ~State() {
    m_storage->writable = false;
    m_storage->scheduleInvalidation = {};
  }

  [[nodiscard]] auto Get() const noexcept -> const T & {
    Detail::TrackRead([this] { return AsReadOnly(); });
    return m_storage->value;
  }

  [[nodiscard]] auto Set(T value) -> bool {
    return SetStorage(m_storage, std::move(value));
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

  [[nodiscard]] auto ObservableNode() const noexcept
      -> const std::shared_ptr<Detail::ObservableNode> & {
    return m_storage->node;
  }
  [[nodiscard]] auto AsReadOnly() const -> ReadOnlyBinding<T>;
  [[nodiscard]] auto AsBinding() -> Binding<T>;

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
          invalidation(invalidationKind), node(Detail::CreateObservableNode()) {
    }

    void Notify() {
      std::vector<std::shared_ptr<StateObserver<T>>> observers;
      observers.reserve(subscribers.size());
      for (const auto &subscriber : subscribers) {
        observers.push_back(subscriber.observer);
      }
      for (const auto &observer : observers) {
        if (*observer) {
          (*observer)(value);
        }
      }
    }

    bool writable{true};
    T value;
    InvalidationScheduler scheduleInvalidation{};
    InvalidationKind invalidation{InvalidationKind::None};
    UInt64 nextSubscriberId{1};
    std::vector<Subscriber> subscribers{};
    std::shared_ptr<Detail::ObservableNode> node{};
  };

  static auto SetStorage(const std::shared_ptr<Storage> &storage, T value)
      -> bool {
    if constexpr (std::equality_comparable<T>) {
      if (storage->value == value) {
        return false;
      }
    }

    storage->value = std::move(value);
    Publish(storage);
    return true;
  }

  static void Publish(const std::shared_ptr<Storage> &source) {
    std::weak_ptr<Storage> weak = source;
    Detail::DispatchObservable(source->node->id, [weak] {
      if (const auto storage = weak.lock()) {
        storage->Notify();
        if (storage->scheduleInvalidation) {
          storage->scheduleInvalidation(storage->invalidation);
        }
      }
    });
  }

  std::shared_ptr<Storage> m_storage;
};

/// @brief Copyable read-only view over an observable value.
template <typename T> class ReadOnlyBinding final {
public:
  using Getter = NGIN::Utilities::Callable<const T &()>;
  using Subscriber = NGIN::Utilities::Callable<Subscription(StateObserver<T>)>;

  ReadOnlyBinding() noexcept = default;
  ReadOnlyBinding(Getter getter, Subscriber subscriber = {},
                  std::shared_ptr<Detail::ObservableNode> node = {})
      : m_getter(std::move(getter)), m_subscriber(std::move(subscriber)),
        m_node(node || !m_subscriber ? std::move(node)
                                     : Detail::CreateObservableNode()) {}

  [[nodiscard]] auto Get() const -> const T & {
    if (m_subscriber && Detail::IsCollectingReads()) {
      Detail::TrackRead([this]() -> const auto & { return *this; });
      Detail::SuspendReadCollection suspend;
      return m_getter();
    }
    return m_getter();
  }
  [[nodiscard]] auto Subscribe(StateObserver<T> observer) const
      -> Subscription {
    return m_subscriber ? m_subscriber(std::move(observer)) : Subscription{};
  }
  [[nodiscard]] auto IsReadable() const noexcept -> bool {
    return static_cast<bool>(m_getter);
  }
  [[nodiscard]] explicit operator bool() const noexcept { return IsReadable(); }
  [[nodiscard]] auto ObservableNode() const noexcept
      -> const std::shared_ptr<Detail::ObservableNode> & {
    return m_node;
  }

private:
  Getter m_getter{};
  Subscriber m_subscriber{};
  std::shared_ptr<Detail::ObservableNode> m_node{};
};

template <typename T> auto State<T>::AsReadOnly() const -> ReadOnlyBinding<T> {
  const auto storage = m_storage;
  return ReadOnlyBinding<T>{
      [storage]() -> const T & { return storage->value; },
      [storage](StateObserver<T> observer) {
        const auto id = storage->nextSubscriberId++;
        storage->subscribers.push_back(typename State<T>::Subscriber{
            .id = id,
            .observer = std::make_shared<StateObserver<T>>(std::move(observer)),
        });
        std::weak_ptr<typename State<T>::Storage> weak = storage;
        return Subscription{[weak = std::move(weak), id] {
          if (const auto locked = weak.lock()) {
            std::erase_if(
                locked->subscribers,
                [id](const typename State<T>::Subscriber &subscriber) {
                  return subscriber.id == id;
                });
          }
        }};
      },
      storage->node,
  };
}

/// @brief Copyable read/write view over observable state or custom accessors.
template <typename T> class Binding final {
public:
  using Getter = NGIN::Utilities::Callable<const T &()>;
  using Setter = NGIN::Utilities::Callable<UIResult<void>(T)>;
  using Subscriber = NGIN::Utilities::Callable<Subscription(StateObserver<T>)>;
  using Validator = NGIN::Utilities::Callable<UIResult<void>(const T &)>;

  Binding() noexcept = default;

  Binding(Getter getter, Setter setter, Subscriber subscriber = {},
          std::shared_ptr<Detail::ObservableNode> node = {})
      : m_getter(std::move(getter)), m_setter(std::move(setter)),
        m_subscriber(std::move(subscriber)),
        m_node(node || !m_subscriber ? std::move(node)
                                     : Detail::CreateObservableNode()) {}

  [[nodiscard]] auto Get() const -> const T & {
    if (m_subscriber && Detail::IsCollectingReads()) {
      Detail::TrackRead([this] { return AsReadOnly(); });
      Detail::SuspendReadCollection suspend;
      return m_getter();
    }
    return m_getter();
  }

  [[nodiscard]] auto Set(T value) const -> UIResult<void> {
    if (!m_setter) {
      return std::unexpected(MakeUIError(UIErrorCode::InvalidState,
                                         "Binding is not writable", "NGIN.UI",
                                         "Binding::Set"));
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
            return std::unexpected(std::move(valid).error());
          }
          if (!setter) {
            return std::unexpected(MakeUIError(UIErrorCode::InvalidState,
                                               "Binding is not writable",
                                               "NGIN.UI", "Binding::Set"));
          }
          return setter(std::move(value));
        },
        m_subscriber,
        m_node,
    };
  }

  [[nodiscard]] auto AsReadOnly() const -> ReadOnlyBinding<T> {
    return ReadOnlyBinding<T>{m_getter, m_subscriber, m_node};
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
  std::shared_ptr<Detail::ObservableNode> m_node{};
};

template <typename T> auto State<T>::AsBinding() -> Binding<T> {
  auto storage = m_storage;
  auto read = AsReadOnly();
  return Binding<T>{[storage]() -> const T & { return storage->value; },
                    [storage](T value) -> UIResult<void> {
                      if (!storage->writable) {
                        return std::unexpected(
                            MakeUIError(UIErrorCode::InvalidState,
                                        "The state owner has been released",
                                        "NGIN.UI", "Binding::Set"));
                      }
                      SetStorage(storage, std::move(value));
                      return {};
                    },
                    [read = std::move(read)](StateObserver<T> observer) {
                      return read.Subscribe(std::move(observer));
                    },
                    storage->node};
}

template <typename T> [[nodiscard]] auto Bind(State<T> &state) -> Binding<T> {
  return state.AsBinding();
}

template <typename T>
[[nodiscard]] auto Observe(State<T> &state) -> ReadOnlyBinding<T> {
  return state.AsReadOnly();
}

/// @brief Type-erased explicit dependency of a computed observable value.
class ObservableDependency final {
public:
  using Observer = NGIN::Utilities::Callable<void()>;
  using Subscriber = NGIN::Utilities::Callable<Subscription(Observer)>;

  ObservableDependency() noexcept = default;
  ObservableDependency(Subscriber subscriber,
                       std::shared_ptr<Detail::ObservableNode> node)
      : m_subscriber(std::move(subscriber)), m_node(std::move(node)) {}

  [[nodiscard]] auto Subscribe(Observer observer) const -> Subscription {
    return m_subscriber ? m_subscriber(std::move(observer)) : Subscription{};
  }
  [[nodiscard]] auto ObservableNode() const noexcept
      -> const std::shared_ptr<Detail::ObservableNode> & {
    return m_node;
  }
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(m_subscriber);
  }

private:
  Subscriber m_subscriber{};
  std::shared_ptr<Detail::ObservableNode> m_node{};
};

template <typename T>
[[nodiscard]] auto DependOn(ReadOnlyBinding<T> binding)
    -> ObservableDependency {
  const auto node = binding.ObservableNode();
  return ObservableDependency{
      [binding = std::move(binding)](
          ObservableDependency::Observer observer) mutable {
        return binding.Subscribe([observer = std::move(observer)](
                                     const T &) mutable { observer(); });
      },
      node,
  };
}

template <typename T>
[[nodiscard]] auto DependOn(State<T> &state) -> ObservableDependency {
  return DependOn(Observe(state));
}

namespace Detail {
/// @brief An active read subscription owned by one mounted component.
struct ObservedRead final {
  UInt64 identity;
  Subscription subscription;
};

/// @brief Installs new subscriptions on first read. Existing subscriptions
/// remain live until Commit has successfully completed the build.
class ReadCollection final {
public:
  ReadCollection(std::vector<ObservedRead> &active,
                 ObservableDependency::Observer invalidate) noexcept;
  ReadCollection(const ReadCollection &) = delete;
  auto operator=(const ReadCollection &) -> ReadCollection & = delete;
  ~ReadCollection();
  void Commit();
  void Add(ObservableDependency dependency);
  std::exception_ptr failure{};

private:
  std::vector<ObservedRead> &m_active;
  ObservableDependency::Observer m_invalidate;
  std::vector<UInt64> m_seen{};
  std::vector<ObservedRead> m_new{};
  ReadCollection *m_previous;
};

void RecordRead(ObservableDependency dependency);

void RecordReadFailure() noexcept;

template <typename Read> void TrackRead(Read &&read) noexcept {
  if (IsCollectingReads()) {
    try {
      const auto &binding = read();
      if (binding.ObservableNode()) {
        RecordRead(DependOn(binding));
      }
    } catch (...) {
      // Reads from existing noexcept model accessors remain safe. Fail the
      // component build before publishing an incomplete subscription set.
      RecordReadFailure();
    }
  }
}
} // namespace Detail

/// @brief Read-only observable value recomputed from explicit dependencies.
template <typename T> class ComputedState final {
public:
  using Compute = NGIN::Utilities::Callable<T()>;

  ComputedState(Compute compute, std::vector<ObservableDependency> dependencies,
                InvalidationScheduler scheduler = {},
                const InvalidationKind invalidation =
                    InvalidationKind::Compose | InvalidationKind::Paint)
      : m_storage(std::make_shared<Storage>(
            std::move(compute), std::move(scheduler), invalidation)) {
    auto configured = SetDependencies(std::move(dependencies));
    if (!configured) {
      m_storage->configurationError = configured.error();
    }
  }

  ComputedState(const ComputedState &) = delete;
  ComputedState(ComputedState &&) noexcept = default;
  auto operator=(const ComputedState &) -> ComputedState & = delete;
  auto operator=(ComputedState &&) noexcept -> ComputedState & = delete;
  ~ComputedState() = default;

  [[nodiscard]] auto Get() const noexcept -> const T & {
    return m_storage->value.Get();
  }
  [[nodiscard]] auto Subscribe(StateObserver<T> observer) -> Subscription {
    return m_storage->value.Subscribe(std::move(observer));
  }
  [[nodiscard]] auto AsReadOnly() -> ReadOnlyBinding<T> {
    const auto storage = m_storage;
    return ReadOnlyBinding<T>{
        [storage]() -> const T & { return storage->value.Get(); },
        [storage](StateObserver<T> observer) {
          return storage->value.Subscribe(std::move(observer));
        },
        storage->value.ObservableNode(),
    };
  }
  [[nodiscard]] auto ConfigurationError() const noexcept
      -> const std::optional<UIError> & {
    return m_storage->configurationError;
  }

  [[nodiscard]] auto
  SetDependencies(std::vector<ObservableDependency> dependencies)
      -> UIResult<void> {
    const auto target = m_storage->value.ObservableNode();
    for (const auto &dependency : dependencies) {
      const auto &node = dependency.ObservableNode();
      if (node && (node == target || Detail::DependsOn(node, target))) {
        return std::unexpected(
            MakeUIError(UIErrorCode::InvalidArgument,
                        "Computed-state dependency cycle detected", "NGIN.UI",
                        "ComputedState::SetDependencies"));
      }
    }

    m_storage->subscriptions.clear();
    target->dependencies.clear();
    m_storage->subscriptions.reserve(dependencies.size());
    target->dependencies.reserve(dependencies.size());
    std::weak_ptr<Storage> weak = m_storage;
    for (const auto &dependency : dependencies) {
      if (dependency.ObservableNode()) {
        target->dependencies.push_back(dependency.ObservableNode());
      }
      m_storage->subscriptions.push_back(dependency.Subscribe([weak] {
        if (const auto storage = weak.lock()) {
          storage->ScheduleRecompute();
        }
      }));
    }
    m_storage->configurationError.reset();
    m_storage->Recompute();
    return {};
  }

private:
  struct Storage final : std::enable_shared_from_this<Storage> {
    Storage(Compute computeValue, InvalidationScheduler scheduler,
            const InvalidationKind invalidation)
        : compute(std::move(computeValue)),
          value(compute(), std::move(scheduler), invalidation) {}

    void ScheduleRecompute() {
      std::weak_ptr<Storage> weak = this->shared_from_this();
      Detail::DispatchObservable(value.ObservableNode()->id, [weak] {
        if (const auto storage = weak.lock()) {
          storage->Recompute();
        }
      });
    }

    void Recompute() { static_cast<void>(value.Set(compute())); }

    Compute compute{};
    State<T> value;
    std::vector<Subscription> subscriptions{};
    std::optional<UIError> configurationError{};
  };

  std::shared_ptr<Storage> m_storage{};
};
} // namespace NGIN::UI
