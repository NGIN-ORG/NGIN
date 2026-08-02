#pragma once

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Events.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace NGIN::UI {
/// @brief Parameter marker used by pages that do not accept navigation data.
struct NoNavigationParameter final {};

/// @brief Stable authored metadata for one registered page.
struct PageRegistrationOptions final {
  std::string id{};
  std::string displayName{};
  std::string routeName{};
};

/// @brief Host-neutral context passed to a page ViewModel factory.
class PageActivationContext {
public:
  virtual ~PageActivationContext() = default;
};

/// @brief ViewModel ownership and deterministic release hook for one page.
template <typename T> struct PageLease final {
  std::shared_ptr<T> viewModel{};
  NGIN::Utilities::Callable<void()> close{};
};

/// @brief Observable description of a registered page.
struct RegisteredPage final {
  std::string id{};
  std::string displayName{};
  std::string routeName{};
  std::type_index pageType{typeid(void)};
  std::type_index viewModelType{typeid(void)};
  std::type_index parameterType{typeid(void)};
};

/// @brief Forward declaration for the typed navigation stack.
class NavigationService;

namespace detail {
/// @brief Internal type-erased owner for one composed page.
class IPageInstance {
public:
  virtual ~IPageInstance() = default;
  virtual void Compose(Composer &composer) = 0;
  virtual void Close() noexcept = 0;
};

template <typename TViewModel, typename TParameter>
/// @brief Internal typed ViewModel, parameter, and composition owner.
class TypedPageInstance final : public IPageInstance {
public:
  using ComposeFunction = NGIN::Utilities::Callable<void(
      Composer &, TViewModel &, const TParameter &)>;

  TypedPageInstance(PageLease<TViewModel> lease,
                    std::shared_ptr<const TParameter> parameter,
                    ComposeFunction compose)
      : m_lease(std::move(lease)), m_parameter(std::move(parameter)),
        m_compose(std::move(compose)) {}
  ~TypedPageInstance() override { Close(); }

  void Compose(Composer &composer) override {
    m_compose(composer, *m_lease.viewModel, *m_parameter);
  }

  void Close() noexcept override {
    if (m_closed) {
      return;
    }
    m_closed = true;
    if (m_lease.close) {
#if NGIN_ASYNC_HAS_EXCEPTIONS
      try {
#endif
        m_lease.close();
#if NGIN_ASYNC_HAS_EXCEPTIONS
      } catch (...) {
      }
#endif
    }
    m_lease.viewModel.reset();
  }

private:
  PageLease<TViewModel> m_lease{};
  std::shared_ptr<const TParameter> m_parameter{};
  ComposeFunction m_compose{};
  bool m_closed{false};
};
} // namespace detail

/// @brief Explicit typed catalogue shared by hosted, standalone, and tests.
class PageRegistry final {
public:
  using ErasedParameter = std::shared_ptr<const void>;
  using ErasedFactory = NGIN::Utilities::Callable<
      UIResult<std::shared_ptr<detail::IPageInstance>>(
          PageActivationContext &, const ErasedParameter &, std::string_view)>;

  PageRegistry() = default;
  PageRegistry(const PageRegistry &) = delete;
  PageRegistry(PageRegistry &&) = delete;
  auto operator=(const PageRegistry &) -> PageRegistry & = delete;
  auto operator=(PageRegistry &&) -> PageRegistry & = delete;

  /// @brief Registers one page tag, ViewModel, parameter, factory, and View.
  template <typename TPage, typename TViewModel,
            typename TParameter = NoNavigationParameter, typename Factory,
            typename Compose>
  [[nodiscard]] auto Register(PageRegistrationOptions options, Factory factory,
                              Compose compose) -> UIResult<void> {
    using Page = std::remove_cvref_t<TPage>;
    using ViewModel = std::remove_cvref_t<TViewModel>;
    using Parameter = std::remove_cvref_t<TParameter>;
    static_assert(
        std::is_invocable_r_v<UIResult<PageLease<ViewModel>>, Factory &,
                              PageActivationContext &, const Parameter &,
                              std::string_view>,
        "Page factory must return UIResult<PageLease<TViewModel>> and accept "
        "(PageActivationContext&, const TParameter&, string_view entryKey)");
    static_assert(std::is_invocable_r_v<void, Compose &, Composer &,
                                        ViewModel &, const Parameter &>,
                  "Page compose function must accept (Composer&, TViewModel&, "
                  "const TParameter&) and return void");

    if constexpr (!(std::is_copy_constructible_v<Factory> &&
                    std::is_copy_constructible_v<Compose>)) {
      static_assert(std::is_copy_constructible_v<Factory> &&
                        std::is_copy_constructible_v<Compose>,
                    "Page factories and compose functions must be copyable");
    }

    if constexpr (requires(Factory &value) { value.operator bool(); }) {
      if (!factory.operator bool()) {
        return MakeUIError(UIErrorCode::InvalidArgument,
                           "Page registration has no ViewModel factory",
                           "NGIN.UI", "PageRegistry::Register");
      }
    }

    auto erased = [factory = std::move(factory), compose = std::move(compose)](
                      PageActivationContext &context,
                      const ErasedParameter &parameter,
                      const std::string_view entryKey)
        -> UIResult<std::shared_ptr<detail::IPageInstance>> {
      const auto typed = std::static_pointer_cast<const Parameter>(parameter);
      if (!typed) {
        return MakeUIError(UIErrorCode::InvalidArgument,
                           "Page navigation parameter is missing", "NGIN.UI",
                           "PageRegistry::Activate");
      }
#if NGIN_ASYNC_HAS_EXCEPTIONS
      try {
#endif
        auto lease = factory(context, *typed, entryKey);
        if (!lease) {
          return NGIN::Utilities::Unexpected<UIError>(lease.Error());
        }
        if (!lease.Value().viewModel) {
          return MakeUIError(UIErrorCode::ResourceFailed,
                             "Page ViewModel factory returned no value",
                             "NGIN.UI", "PageRegistry::Activate");
        }
        return std::static_pointer_cast<detail::IPageInstance>(
            std::make_shared<detail::TypedPageInstance<ViewModel, Parameter>>(
                std::move(lease).Value(), typed, std::move(compose)));
#if NGIN_ASYNC_HAS_EXCEPTIONS
      } catch (...) {
        return MakeUIError(UIErrorCode::ResourceFailed,
                           "Page activation failed", "NGIN.UI",
                           "PageRegistry::Activate");
      }
#endif
    };

    return RegisterErased(
        RegisteredPage{.id = std::move(options.id),
                       .displayName = std::move(options.displayName),
                       .routeName = std::move(options.routeName),
                       .pageType = std::type_index{typeid(Page)},
                       .viewModelType = std::type_index{typeid(ViewModel)},
                       .parameterType = std::type_index{typeid(Parameter)}},
        std::move(erased));
  }

  void Freeze() noexcept;
  [[nodiscard]] auto IsFrozen() const noexcept -> bool;
  [[nodiscard]] auto Pages() const noexcept
      -> const std::vector<RegisteredPage> &;
  [[nodiscard]] auto FindById(std::string_view id) const noexcept
      -> const RegisteredPage *;
  [[nodiscard]] auto FindByRoute(std::string_view route) const noexcept
      -> const RegisteredPage *;

private:
  struct Entry final {
    RegisteredPage metadata{};
    ErasedFactory factory{};
  };

  [[nodiscard]] auto RegisterErased(RegisteredPage page, ErasedFactory factory)
      -> UIResult<void>;
  [[nodiscard]] auto Find(std::type_index pageType) const noexcept
      -> const Entry *;

  std::vector<Entry> m_entries{};
  std::vector<RegisteredPage> m_pages{};
  bool m_frozen{false};

  friend class NavigationService;
};

/// @brief Result of one successful stack mutation.
struct NavigationChange final {
  UInt64 entryId{0};
  UIntSize stackDepth{0};
  bool restoredFromCache{false};
};

/// @brief Immutable page entry used by diagnostics and headless tests.
struct NavigationEntrySnapshot final {
  UInt64 entryId{0};
  std::string pageId{};
  std::string displayName{};
  std::string cacheKey{};
  bool visible{false};
  bool cached{false};
};

/// @brief Current window-local or region-local navigation state.
struct NavigationSnapshot final {
  std::string region{};
  std::vector<NavigationEntrySnapshot> stack{};
  std::vector<NavigationEntrySnapshot> cache{};
};

/// @brief Region, cache, scheduling, and invalidation policy for one stack.
struct NavigationOptions final {
  std::string region{"Main"};
  UIntSize cacheCapacity{0};
  NGIN::Utilities::Callable<bool()> isOnScheduler{};
  InvalidationScheduler invalidate{};
};

/// @brief Typed, rollback-safe page stack for one window or named region.
class NavigationService final {
public:
  using Observer = NGIN::Utilities::Callable<void(const NavigationSnapshot &)>;
  using FailureObserver = NGIN::Utilities::Callable<void(const UIError &)>;

  NavigationService(PageRegistry &registry, PageActivationContext &context,
                    NavigationOptions options = {});
  NavigationService(const NavigationService &) = delete;
  NavigationService(NavigationService &&) = delete;
  auto operator=(const NavigationService &) -> NavigationService & = delete;
  auto operator=(NavigationService &&) -> NavigationService & = delete;
  ~NavigationService();

  template <typename TPage>
  [[nodiscard]] auto Start(std::string cacheKey = {})
      -> UIResult<NavigationChange> {
    return Start<TPage>(NoNavigationParameter{}, std::move(cacheKey));
  }

  template <typename TPage, typename TParameter>
  [[nodiscard]] auto Start(TParameter parameter, std::string cacheKey = {})
      -> UIResult<NavigationChange> {
    return Mutate(Mutation::Start, std::type_index{typeid(TPage)},
                  std::type_index{typeid(std::remove_cvref_t<TParameter>)},
                  std::make_shared<const std::remove_cvref_t<TParameter>>(
                      std::move(parameter)),
                  std::move(cacheKey));
  }

  template <typename TPage>
  [[nodiscard]] auto Navigate(std::string cacheKey = {})
      -> UIResult<NavigationChange> {
    return Navigate<TPage>(NoNavigationParameter{}, std::move(cacheKey));
  }

  template <typename TPage, typename TParameter>
  [[nodiscard]] auto Navigate(TParameter parameter, std::string cacheKey = {})
      -> UIResult<NavigationChange> {
    return Mutate(Mutation::Push, std::type_index{typeid(TPage)},
                  std::type_index{typeid(std::remove_cvref_t<TParameter>)},
                  std::make_shared<const std::remove_cvref_t<TParameter>>(
                      std::move(parameter)),
                  std::move(cacheKey));
  }

  template <typename TPage>
  [[nodiscard]] auto Replace(std::string cacheKey = {})
      -> UIResult<NavigationChange> {
    return Replace<TPage>(NoNavigationParameter{}, std::move(cacheKey));
  }

  template <typename TPage, typename TParameter>
  [[nodiscard]] auto Replace(TParameter parameter, std::string cacheKey = {})
      -> UIResult<NavigationChange> {
    return Mutate(Mutation::Replace, std::type_index{typeid(TPage)},
                  std::type_index{typeid(std::remove_cvref_t<TParameter>)},
                  std::make_shared<const std::remove_cvref_t<TParameter>>(
                      std::move(parameter)),
                  std::move(cacheKey));
  }

  [[nodiscard]] auto Back() -> UIResult<NavigationChange>;
  [[nodiscard]] auto Clear() -> UIResult<NavigationChange>;
  [[nodiscard]] auto HandleBackEvent(const PlatformEvent &event)
      -> UIResult<bool>;
  void Compose(Composer &composer);
  void SetObserver(Observer observer);
  void SetFailureObserver(FailureObserver observer);
  [[nodiscard]] auto Snapshot() const -> NavigationSnapshot;
  [[nodiscard]] auto CanGoBack() const noexcept -> bool;
  [[nodiscard]] auto StackDepth() const noexcept -> UIntSize;
  [[nodiscard]] auto Region() const noexcept -> std::string_view;

private:
  enum class Mutation : UInt8 { Start, Push, Replace };
  struct Entry;
  class MutationGuard;

  [[nodiscard]] auto Mutate(Mutation mutation, std::type_index pageType,
                            std::type_index parameterType,
                            PageRegistry::ErasedParameter parameter,
                            std::string cacheKey) -> UIResult<NavigationChange>;
  [[nodiscard]] auto BeginMutation(const char *operation)
      -> UIResult<MutationGuard>;
  [[nodiscard]] auto Fail(UIError error) -> UIResult<NavigationChange>;
  void Retire(std::shared_ptr<Entry> entry) noexcept;
  void TrimCache() noexcept;
  void Changed();
  void CloseAll() noexcept;

  PageRegistry *m_registry{nullptr};
  PageActivationContext *m_context{nullptr};
  NavigationOptions m_options{};
  std::vector<std::shared_ptr<Entry>> m_stack{};
  std::vector<std::shared_ptr<Entry>> m_cache{};
  Observer m_observer{};
  FailureObserver m_failureObserver{};
  std::atomic_bool m_mutating{false};
  UInt64 m_nextEntryId{1};
};

/// @brief Small adapter for binding a navigation stack to window content/input.
class NavigationHost final {
public:
  explicit NavigationHost(NavigationService &navigation) noexcept;
  void Compose(Composer &composer);
  [[nodiscard]] auto HandleEvent(const PlatformEvent &event) -> UIResult<bool>;

private:
  NavigationService *m_navigation{nullptr};
};
} // namespace NGIN::UI
