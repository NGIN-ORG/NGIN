#pragma once

#include <NGIN/UI/Navigation.hpp>

#include <memory>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::UI::Testing {
/// @brief Headless activation context with typed service overrides and lease
/// accounting.
class PageTestContext final : public PageActivationContext {
public:
  PageTestContext() : m_state(std::make_shared<State>()) {}

  /// @brief Replaces one typed test service for later page factories.
  template <typename T> void Override(std::shared_ptr<T> service) {
    m_services.insert_or_assign(std::type_index{typeid(T)}, std::move(service));
  }

  /// @brief Resolves one required typed test service.
  template <typename T>
  [[nodiscard]] auto ResolveRequired() const -> UIResult<std::shared_ptr<T>> {
    const auto found = m_services.find(std::type_index{typeid(T)});
    if (found == m_services.end()) {
      return MakeUIError(UIErrorCode::ResourceFailed,
                         "Required page test service is not registered",
                         "NGIN.UI.Testing", "PageTestContext::ResolveRequired");
    }
    return std::static_pointer_cast<T>(found->second);
  }

  /// @brief Wraps a test ViewModel in a counted page lease.
  template <typename T>
  [[nodiscard]] auto Lease(std::shared_ptr<T> viewModel) -> PageLease<T> {
    ++m_state->created;
    ++m_state->active;
    auto released = std::make_shared<bool>(false);
    return PageLease<T>{
        .viewModel = std::move(viewModel),
        .close =
            [state = m_state, released] {
              if (*released) {
                return;
              }
              *released = true;
              ++state->released;
              --state->active;
            },
    };
  }

  [[nodiscard]] auto ActiveLeaseCount() const noexcept -> UIntSize {
    return m_state->active;
  }
  [[nodiscard]] auto CreatedLeaseCount() const noexcept -> UInt64 {
    return m_state->created;
  }
  [[nodiscard]] auto ReleasedLeaseCount() const noexcept -> UInt64 {
    return m_state->released;
  }

  /// @brief Fails when a headless page scope remains active.
  [[nodiscard]] auto AssertNoScopeLeaks() const -> UIResult<void> {
    if (m_state->active != 0 || m_state->created != m_state->released) {
      return MakeUIError(
          UIErrorCode::InvalidState, "Headless page leases are still active",
          "NGIN.UI.Testing", "PageTestContext::AssertNoScopeLeaks");
    }
    return {};
  }

private:
  /// @brief Shared counters retained by outstanding test leases.
  struct State final {
    UIntSize active{0};
    UInt64 created{0};
    UInt64 released{0};
  };

  std::unordered_map<std::type_index, std::shared_ptr<void>> m_services{};
  std::shared_ptr<State> m_state{};
};

/// @brief Concise initial-page and stack assertions for headless tests.
class NavigationTestDriver final {
public:
  explicit NavigationTestDriver(NavigationService &navigation) noexcept
      : m_navigation(&navigation) {}

  /// @brief Selects the typed initial page.
  template <typename TPage>
  [[nodiscard]] auto SelectInitial() -> UIResult<NavigationChange> {
    return m_navigation->Start<TPage>();
  }

  /// @brief Selects the typed initial page and parameter.
  template <typename TPage, typename TParameter>
  [[nodiscard]] auto SelectInitial(TParameter parameter)
      -> UIResult<NavigationChange> {
    return m_navigation->Start<TPage>(std::move(parameter));
  }

  /// @brief Verifies the exact ordered stack of stable page IDs.
  [[nodiscard]] auto
  AssertStack(const std::vector<std::string_view> &expected) const
      -> UIResult<void> {
    const auto snapshot = m_navigation->Snapshot();
    if (snapshot.stack.size() != expected.size()) {
      return MakeUIError(
          UIErrorCode::InvalidState, "Navigation stack depth does not match",
          "NGIN.UI.Testing", "NavigationTestDriver::AssertStack");
    }
    for (UIntSize index = 0; index < expected.size(); ++index) {
      if (snapshot.stack[index].pageId != expected[index]) {
        return MakeUIError(
            UIErrorCode::InvalidState, "Navigation stack page does not match",
            "NGIN.UI.Testing", "NavigationTestDriver::AssertStack");
      }
    }
    return {};
  }

  [[nodiscard]] auto Snapshot() const -> NavigationSnapshot {
    return m_navigation->Snapshot();
  }

private:
  NavigationService *m_navigation{nullptr};
};
} // namespace NGIN::UI::Testing
