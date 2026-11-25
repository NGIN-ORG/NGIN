#include <NGIN/UI/State.hpp>

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NGIN::UI::Detail {
namespace {
struct BatchContext final {
  UIntSize depth{0};
  bool flushing{false};
  std::vector<UInt64> order{};
  std::unordered_map<UInt64, NGIN::Utilities::Callable<void()>> pending{};
};

thread_local BatchContext Batch{};
thread_local ReadCollection *ActiveReadCollection{};
std::atomic<UInt64> NextObservableId{1};
std::atomic<UInt64> ActiveSubscriptions{0};
std::atomic<UInt64> PeakSubscriptions{0};
std::atomic<UInt64> CreatedSubscriptions{0};
std::atomic<UInt64> CanceledSubscriptions{0};
} // namespace

SuspendReadCollection::SuspendReadCollection() noexcept
    : m_previous(std::exchange(ActiveReadCollection, nullptr)) {}
SuspendReadCollection::~SuspendReadCollection() {
  ActiveReadCollection = m_previous;
}

ReadCollection::ReadCollection(
    std::vector<ObservedRead> &active,
    ObservableDependency::Observer invalidate) noexcept
    : m_active(active), m_invalidate(std::move(invalidate)),
      m_previous(std::exchange(ActiveReadCollection, this)) {}
ReadCollection::~ReadCollection() { ActiveReadCollection = m_previous; }

void ReadCollection::Add(ObservableDependency dependency) {
  const auto identity = dependency.ObservableNode()->id;
  if (std::ranges::find(m_seen, identity) != m_seen.end())
    return;
  m_seen.push_back(identity);
  if (std::ranges::find(m_active, identity, &ObservedRead::identity) ==
      m_active.end()) {
    SuspendReadCollection suspend;
    m_new.push_back({identity, dependency.Subscribe(m_invalidate)});
  }
}

void ReadCollection::Commit() {
  if (failure)
    std::rethrow_exception(failure);
  m_active.reserve(m_seen.size());
  std::erase_if(m_active, [&](const ObservedRead &old) {
    return std::ranges::find(m_seen, old.identity) == m_seen.end();
  });
  for (auto &connection : m_new)
    m_active.push_back(std::move(connection));
}

auto IsCollectingReads() noexcept -> bool {
  return ActiveReadCollection != nullptr;
}
void RecordReadFailure() noexcept {
  ActiveReadCollection->failure = std::current_exception();
}
void RecordRead(ObservableDependency dependency) {
  ActiveReadCollection->Add(std::move(dependency));
}

auto CreateObservableNode() -> std::shared_ptr<ObservableNode> {
  return std::make_shared<ObservableNode>(ObservableNode{
      .id = NextObservableId.fetch_add(1, std::memory_order_relaxed),
  });
}

auto DependsOn(const std::shared_ptr<ObservableNode> &start,
               const std::shared_ptr<ObservableNode> &target) -> bool {
  if (!start || !target) {
    return false;
  }
  std::vector<std::shared_ptr<ObservableNode>> pending{start};
  std::unordered_set<UInt64> visited{};
  while (!pending.empty()) {
    auto current = std::move(pending.back());
    pending.pop_back();
    if (!current || !visited.insert(current->id).second) {
      continue;
    }
    if (current == target) {
      return true;
    }
    for (const auto &dependency : current->dependencies) {
      if (auto locked = dependency.lock()) {
        pending.push_back(std::move(locked));
      }
    }
  }
  return false;
}

void DispatchObservable(const UInt64 identity,
                        NGIN::Utilities::Callable<void()> publish) {
  if (Batch.depth == 0 && !Batch.flushing) {
    publish();
    return;
  }
  if (!Batch.pending.contains(identity)) {
    Batch.order.push_back(identity);
    Batch.pending.emplace(identity, std::move(publish));
  }
}

void BeginStateBatch() noexcept { ++Batch.depth; }

void RegisterSubscription() noexcept {
  CreatedSubscriptions.fetch_add(1, std::memory_order_relaxed);
  const auto active =
      ActiveSubscriptions.fetch_add(1, std::memory_order_relaxed) + 1;
  auto peak = PeakSubscriptions.load(std::memory_order_relaxed);
  while (active > peak && !PeakSubscriptions.compare_exchange_weak(
                              peak, active, std::memory_order_relaxed,
                              std::memory_order_relaxed)) {
  }
}

void UnregisterSubscription() noexcept {
  ActiveSubscriptions.fetch_sub(1, std::memory_order_relaxed);
  CanceledSubscriptions.fetch_add(1, std::memory_order_relaxed);
}

void EndStateBatch() {
  if (Batch.depth == 0) {
    return;
  }
  --Batch.depth;
  if (Batch.depth != 0) {
    return;
  }

  Batch.flushing = true;
  try {
    UIntSize index = 0;
    while (index < Batch.order.size()) {
      const auto identity = Batch.order[index++];
      const auto found = Batch.pending.find(identity);
      if (found == Batch.pending.end()) {
        continue;
      }
      auto publish = std::move(found->second);
      Batch.pending.erase(found);
      publish();
    }
  } catch (...) {
    Batch.order.clear();
    Batch.pending.clear();
    Batch.flushing = false;
    throw;
  }
  Batch.order.clear();
  Batch.pending.clear();
  Batch.flushing = false;
}
} // namespace NGIN::UI::Detail

namespace NGIN::UI {
StateBatch::StateBatch() noexcept { Detail::BeginStateBatch(); }
StateBatch::~StateBatch() noexcept(false) { Detail::EndStateBatch(); }

auto CurrentSubscriptionDiagnostics() noexcept -> SubscriptionDiagnostics {
  return SubscriptionDiagnostics{
      .activeCount =
          Detail::ActiveSubscriptions.load(std::memory_order_relaxed),
      .peakActiveCount =
          Detail::PeakSubscriptions.load(std::memory_order_relaxed),
      .createdCount =
          Detail::CreatedSubscriptions.load(std::memory_order_relaxed),
      .canceledCount =
          Detail::CanceledSubscriptions.load(std::memory_order_relaxed),
  };
}
} // namespace NGIN::UI
