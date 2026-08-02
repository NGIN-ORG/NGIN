#include <NGIN/UI/Navigation.hpp>

#include <algorithm>

namespace NGIN::UI {
auto PageRegistry::RegisterErased(RegisteredPage page, ErasedFactory factory)
    -> UIResult<void> {
  if (m_frozen) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Page registry is already frozen", "NGIN.UI",
                       "PageRegistry::Register");
  }
  if (page.id.empty()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Page identity cannot be empty", "NGIN.UI",
                       "PageRegistry::Register");
  }
  if (!factory) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Page registration has no ViewModel factory", "NGIN.UI",
                       "PageRegistry::Register", page.id.c_str());
  }
  const auto duplicate =
      std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &entry) {
        return entry.metadata.id == page.id ||
               entry.metadata.pageType == page.pageType;
      });
  if (duplicate != m_entries.end()) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Page identity or page tag is already registered",
                       "NGIN.UI", "PageRegistry::Register", page.id.c_str());
  }
  if (!page.routeName.empty()) {
    const auto route = std::find_if(
        m_entries.begin(), m_entries.end(), [&](const Entry &entry) {
          return entry.metadata.routeName == page.routeName;
        });
    if (route != m_entries.end()) {
      return MakeUIError(UIErrorCode::InvalidArgument,
                         "Page route name is already registered", "NGIN.UI",
                         "PageRegistry::Register", page.routeName.c_str());
    }
  }
#if NGIN_ASYNC_HAS_EXCEPTIONS
  try {
#endif
    m_pages.push_back(page);
    m_entries.push_back(
        Entry{.metadata = std::move(page), .factory = std::move(factory)});
    return {};
#if NGIN_ASYNC_HAS_EXCEPTIONS
  } catch (...) {
    if (m_pages.size() > m_entries.size()) {
      m_pages.pop_back();
    }
    return MakeUIError(UIErrorCode::OutOfMemory,
                       "Page registration allocation failed", "NGIN.UI",
                       "PageRegistry::Register");
  }
#endif
}

void PageRegistry::Freeze() noexcept { m_frozen = true; }
auto PageRegistry::IsFrozen() const noexcept -> bool { return m_frozen; }
auto PageRegistry::Pages() const noexcept
    -> const std::vector<RegisteredPage> & {
  return m_pages;
}

auto PageRegistry::Find(const std::type_index pageType) const noexcept
    -> const Entry * {
  const auto found =
      std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &entry) {
        return entry.metadata.pageType == pageType;
      });
  return found == m_entries.end() ? nullptr : &*found;
}

auto PageRegistry::FindById(const std::string_view id) const noexcept
    -> const RegisteredPage * {
  const auto found =
      std::find_if(m_pages.begin(), m_pages.end(),
                   [&](const RegisteredPage &page) { return page.id == id; });
  return found == m_pages.end() ? nullptr : &*found;
}

auto PageRegistry::FindByRoute(const std::string_view route) const noexcept
    -> const RegisteredPage * {
  const auto found = std::find_if(
      m_pages.begin(), m_pages.end(), [&](const RegisteredPage &page) {
        return !page.routeName.empty() && page.routeName == route;
      });
  return found == m_pages.end() ? nullptr : &*found;
}

struct NavigationService::Entry final {
  UInt64 id{0};
  const PageRegistry::Entry *registration{nullptr};
  std::string cacheKey{};
  std::shared_ptr<detail::IPageInstance> instance{};
};

class NavigationService::MutationGuard final {
public:
  MutationGuard() noexcept = default;
  explicit MutationGuard(std::atomic_bool &flag) noexcept : m_flag(&flag) {}
  MutationGuard(const MutationGuard &) = delete;
  MutationGuard(MutationGuard &&other) noexcept
      : m_flag(std::exchange(other.m_flag, nullptr)) {}
  auto operator=(const MutationGuard &) -> MutationGuard & = delete;
  auto operator=(MutationGuard &&) -> MutationGuard & = delete;
  ~MutationGuard() {
    if (m_flag != nullptr) {
      m_flag->store(false);
    }
  }

private:
  std::atomic_bool *m_flag{nullptr};
};

NavigationService::NavigationService(PageRegistry &registry,
                                     PageActivationContext &context,
                                     NavigationOptions options)
    : m_registry(&registry), m_context(&context),
      m_options(std::move(options)) {
  m_registry->Freeze();
}

NavigationService::~NavigationService() { CloseAll(); }

auto NavigationService::BeginMutation(const char *operation)
    -> UIResult<MutationGuard> {
  if (m_options.isOnScheduler && !m_options.isOnScheduler()) {
    return MakeUIError(UIErrorCode::WrongThread,
                       "Navigation must run on its UI scheduler", "NGIN.UI",
                       operation, m_options.region.c_str());
  }
  if (m_mutating.exchange(true)) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "A navigation operation is already running", "NGIN.UI",
                       operation, m_options.region.c_str());
  }
  return MutationGuard{m_mutating};
}

auto NavigationService::Fail(UIError error) -> UIResult<NavigationChange> {
  if (m_failureObserver) {
    m_failureObserver(error);
  }
  return NGIN::Utilities::Unexpected<UIError>(std::move(error));
}

auto NavigationService::Mutate(const Mutation mutation,
                               const std::type_index pageType,
                               const std::type_index parameterType,
                               PageRegistry::ErasedParameter parameter,
                               std::string cacheKey)
    -> UIResult<NavigationChange> {
  auto guard = BeginMutation("NavigationService::Navigate");
  if (!guard) {
    return Fail(guard.Error());
  }
  if (mutation == Mutation::Start && !m_stack.empty()) {
    return Fail(MakeUIError(UIErrorCode::InvalidState,
                            "Startup page can only be set on an empty stack",
                            "NGIN.UI", "NavigationService::Start",
                            m_options.region.c_str()));
  }
  const auto *registration = m_registry->Find(pageType);
  if (registration == nullptr) {
    return Fail(MakeUIError(
        UIErrorCode::InvalidArgument, "Page tag is not registered", "NGIN.UI",
        "NavigationService::Navigate", m_options.region.c_str()));
  }
  if (registration->metadata.parameterType != parameterType) {
    return Fail(MakeUIError(UIErrorCode::InvalidArgument,
                            "Navigation parameter type does not match page",
                            "NGIN.UI", "NavigationService::Navigate",
                            registration->metadata.id.c_str()));
  }

  std::shared_ptr<Entry> next{};
  bool restored = false;
  if (!cacheKey.empty()) {
    const auto cached =
        std::find_if(m_cache.begin(), m_cache.end(),
                     [&](const std::shared_ptr<Entry> &entry) {
                       return entry->registration == registration &&
                              entry->cacheKey == cacheKey;
                     });
    if (cached != m_cache.end()) {
      next = *cached;
      m_cache.erase(cached);
      restored = true;
    }
  }
  if (!next) {
    const auto entryId = m_nextEntryId;
    const auto key = m_options.region + "." + registration->metadata.id + "." +
                     std::to_string(entryId);
    auto activated = registration->factory(*m_context, parameter, key);
    if (!activated) {
      return Fail(activated.Error());
    }
    auto instance = std::move(activated).Value();
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      next = std::make_shared<Entry>(Entry{.id = entryId,
                                           .registration = registration,
                                           .cacheKey = std::move(cacheKey),
                                           .instance = instance});
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      instance->Close();
      return Fail(MakeUIError(
          UIErrorCode::OutOfMemory, "Page entry allocation failed", "NGIN.UI",
          "NavigationService::Navigate", registration->metadata.id.c_str()));
    }
#endif
    ++m_nextEntryId;
  }

  if (mutation == Mutation::Replace && !m_stack.empty()) {
    auto previous = std::move(m_stack.back());
    m_stack.back() = next;
    Retire(std::move(previous));
  } else {
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      m_stack.push_back(next);
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
      next->instance->Close();
      return Fail(MakeUIError(UIErrorCode::OutOfMemory,
                              "Navigation stack allocation failed", "NGIN.UI",
                              "NavigationService::Navigate",
                              registration->metadata.id.c_str()));
    }
#endif
  }
  const NavigationChange change{.entryId = m_stack.back()->id,
                                .stackDepth = m_stack.size(),
                                .restoredFromCache = restored};
  Changed();
  return change;
}

auto NavigationService::Back() -> UIResult<NavigationChange> {
  auto guard = BeginMutation("NavigationService::Back");
  if (!guard) {
    return Fail(guard.Error());
  }
  if (m_stack.size() <= 1) {
    return Fail(MakeUIError(
        UIErrorCode::InvalidState, "Navigation stack has no page to return to",
        "NGIN.UI", "NavigationService::Back", m_options.region.c_str()));
  }
  auto removed = std::move(m_stack.back());
  m_stack.pop_back();
  Retire(std::move(removed));
  const NavigationChange change{.entryId = m_stack.back()->id,
                                .stackDepth = m_stack.size()};
  Changed();
  return change;
}

auto NavigationService::Clear() -> UIResult<NavigationChange> {
  auto guard = BeginMutation("NavigationService::Clear");
  if (!guard) {
    return Fail(guard.Error());
  }
  for (auto &entry : m_stack) {
    Retire(std::move(entry));
  }
  m_stack.clear();
  Changed();
  return NavigationChange{.stackDepth = 0};
}

auto NavigationService::HandleBackEvent(const PlatformEvent &event)
    -> UIResult<bool> {
  const auto *key = std::get_if<KeyChanged>(&event);
  if (key == nullptr || key->state != KeyState::Pressed) {
    return false;
  }
  const auto isEscape = key->Logical() == LogicalKey::Escape;
  const auto isAltLeft = key->Logical() == LogicalKey::Left &&
                         HasKeyModifier(key->modifiers, KeyModifierFlags::Alt);
  if ((!isEscape && !isAltLeft) || !CanGoBack()) {
    return false;
  }
  auto result = Back();
  if (!result) {
    return NGIN::Utilities::Unexpected<UIError>(result.Error());
  }
  return true;
}

void NavigationService::Compose(Composer &composer) {
  for (UIntSize index = 0; index < m_stack.size(); ++index) {
    NodeProperties properties{};
    properties.visibility = index + 1 == m_stack.size()
                                ? ElementVisibility::Visible
                                : ElementVisibility::Collapsed;
    const auto key = "navigation.entry." + std::to_string(m_stack[index]->id);
    composer.Element(
        ElementType::Column, properties,
        [&] { m_stack[index]->instance->Compose(composer); }, key);
  }
  for (const auto &entry : m_cache) {
    NodeProperties properties{};
    properties.visibility = ElementVisibility::Collapsed;
    const auto key = "navigation.cache." + std::to_string(entry->id);
    composer.Element(
        ElementType::Column, properties,
        [&] { entry->instance->Compose(composer); }, key);
  }
}

void NavigationService::Retire(std::shared_ptr<Entry> entry) noexcept {
  if (!entry) {
    return;
  }
  if (m_options.cacheCapacity > 0 && !entry->cacheKey.empty()) {
#if NGIN_ASYNC_HAS_EXCEPTIONS
    try {
#endif
      m_cache.push_back(std::move(entry));
      TrimCache();
      return;
#if NGIN_ASYNC_HAS_EXCEPTIONS
    } catch (...) {
    }
#endif
  }
  if (entry) {
    entry->instance->Close();
  }
}

void NavigationService::TrimCache() noexcept {
  while (m_cache.size() > m_options.cacheCapacity) {
    m_cache.front()->instance->Close();
    m_cache.erase(m_cache.begin());
  }
}

void NavigationService::Changed() {
  if (m_options.invalidate) {
    m_options.invalidate(InvalidationKind::Compose);
  }
  if (m_observer) {
    m_observer(Snapshot());
  }
}

void NavigationService::CloseAll() noexcept {
  for (auto &entry : m_stack) {
    entry->instance->Close();
  }
  for (auto &entry : m_cache) {
    entry->instance->Close();
  }
  m_stack.clear();
  m_cache.clear();
}

void NavigationService::SetObserver(Observer observer) {
  m_observer = std::move(observer);
}

void NavigationService::SetFailureObserver(FailureObserver observer) {
  m_failureObserver = std::move(observer);
}

auto NavigationService::Snapshot() const -> NavigationSnapshot {
  NavigationSnapshot snapshot{.region = m_options.region};
  snapshot.stack.reserve(m_stack.size());
  snapshot.cache.reserve(m_cache.size());
  for (UIntSize index = 0; index < m_stack.size(); ++index) {
    const auto &entry = m_stack[index];
    snapshot.stack.push_back(NavigationEntrySnapshot{
        .entryId = entry->id,
        .pageId = entry->registration->metadata.id,
        .displayName = entry->registration->metadata.displayName,
        .cacheKey = entry->cacheKey,
        .visible = index + 1 == m_stack.size(),
        .cached = false});
  }
  for (const auto &entry : m_cache) {
    snapshot.cache.push_back(NavigationEntrySnapshot{
        .entryId = entry->id,
        .pageId = entry->registration->metadata.id,
        .displayName = entry->registration->metadata.displayName,
        .cacheKey = entry->cacheKey,
        .visible = false,
        .cached = true});
  }
  return snapshot;
}

auto NavigationService::CanGoBack() const noexcept -> bool {
  return m_stack.size() > 1;
}
auto NavigationService::StackDepth() const noexcept -> UIntSize {
  return m_stack.size();
}
auto NavigationService::Region() const noexcept -> std::string_view {
  return m_options.region;
}

NavigationHost::NavigationHost(NavigationService &navigation) noexcept
    : m_navigation(&navigation) {}
void NavigationHost::Compose(Composer &composer) {
  m_navigation->Compose(composer);
}
auto NavigationHost::HandleEvent(const PlatformEvent &event) -> UIResult<bool> {
  return m_navigation->HandleBackEvent(event);
}
} // namespace NGIN::UI
