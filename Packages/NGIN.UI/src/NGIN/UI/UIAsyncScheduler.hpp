#pragma once

#include <NGIN/Execution/TimerRegistration.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <NGIN/Execution/detail/WorkBudget.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

namespace NGIN::UI::Detail {
// UI-clock adapter with bounded admission and reserved cleanup delivery.
// Bind before use; the platform and scheduler outlive every admitted delivery.
class UIAsyncScheduler final {
public:
  using ClockTime = std::chrono::nanoseconds;
  explicit UIAsyncScheduler(std::size_t capacity = 4096)
      : m_capacity(capacity), m_completions(capacity, &Notify, this) {}

  template <class Platform> void Bind(Platform &platform) noexcept {
    m_platform = &platform;
    m_now = +[](void *value) noexcept {
      return static_cast<Platform *>(value)->MonotonicNow();
    };
    m_wake = +[](void *value) noexcept {
      static_cast<Platform *>(value)->WakeEventLoop();
    };
  }
  [[nodiscard]] auto
  ReserveCompletion(NGIN::Execution::WorkItem work) noexcept {
    return m_completions.Reserve(std::move(work));
  }
  [[nodiscard]] bool IsCurrent() const noexcept { return s_current == this; }

  [[nodiscard]] NGIN::Execution::ScheduleResult
  Execute(NGIN::Execution::WorkItem work) noexcept {
    if (work.IsEmpty())
      return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
    try {
      std::lock_guard lock(m_mutex);
      if (m_closed)
        return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
      if (m_ready.size() == m_capacity)
        return std::unexpected(
            NGIN::Execution::ScheduleError::ResourceExhausted);
      m_ready.push_back(std::move(work));
    } catch (const std::bad_alloc &) {
      return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
    }
    Wake();
    return {};
  }
  [[nodiscard]] auto ScheduleTimer(NGIN::Execution::WorkItem work,
                                   NGIN::Time::TimePoint at) noexcept
      -> std::expected<NGIN::Execution::TimerRegistration,
                       NGIN::Execution::ScheduleError> {
    if (work.IsEmpty())
      return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
    const auto now = NGIN::Time::MonotonicClock::Now();
    const auto platformNow = m_now ? m_now(m_platform) : ClockTime{};
    const auto ticks = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, platformNow.count()));
    const auto maximum =
        static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
    const auto delta = at > now ? at.ToNanoseconds() - now.ToNanoseconds() : 0;
    const auto due = ClockTime(
        static_cast<std::int64_t>(ticks + std::min(delta, maximum - ticks)));
    std::uint64_t identifier;
    try {
      std::lock_guard lock(m_mutex);
      if (m_closed)
        return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
      if (m_delayed.size() == m_capacity ||
          m_identifier == (std::numeric_limits<std::uint64_t>::max)())
        return std::unexpected(
            NGIN::Execution::ScheduleError::ResourceExhausted);
      identifier = ++m_identifier;
      m_delayed.push_back({due, std::move(work), identifier});
    } catch (const std::bad_alloc &) {
      return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
    }
    Wake();
    return NGIN::Execution::TimerRegistration(
        this, 0, identifier,
        +[](void *value, std::uint64_t, std::uint64_t id) noexcept {
          return static_cast<UIAsyncScheduler *>(value)->CancelTimer(id);
        });
  }
  [[nodiscard]] NGIN::Execution::ScheduleResult
  ExecuteAt(NGIN::Execution::WorkItem work, NGIN::Time::TimePoint at) noexcept {
    auto timer = ScheduleTimer(std::move(work), at);
    if (!timer)
      return std::unexpected(timer.error());
    timer->Detach();
    return {};
  }
  [[nodiscard]] std::optional<ClockTime> NextDeadline() const noexcept {
    if (m_completions.HasReady())
      return ClockTime{};
    std::lock_guard lock(m_mutex);
    if (!m_ready.empty())
      return ClockTime{};
    std::optional<ClockTime> result;
    for (const auto &entry : m_delayed)
      if (!result || entry.due < *result)
        result = entry.due;
    return result;
  }
  void RunReady(ClockTime now) noexcept {
    for (std::size_t count = 0; count != 64 && RunOne(now); ++count) {
    }
    if (const auto next = NextDeadline(); next && *next <= now)
      Wake();
  }
  void DrainReady(ClockTime now) noexcept {
    while (RunOne(now)) {
    }
  }
  void Shutdown() noexcept {
    std::deque<NGIN::Execution::WorkItem> ready;
    std::vector<DelayedEntry> delayed;
    {
      std::lock_guard lock(m_mutex);
      m_closed = true;
      ready.swap(m_ready);
      delayed.swap(m_delayed);
    }
    m_completions.Close();
    // Discard callbacks may publish reserved cleanup; never destroy under a
    // queue lock.
    ready.clear();
    delayed.clear();
  }

private:
  struct DelayedEntry {
    ClockTime due;
    NGIN::Execution::WorkItem item;
    std::uint64_t identifier;
  };
  bool CancelTimer(std::uint64_t identifier) noexcept {
    NGIN::Execution::WorkItem retired;
    {
      std::lock_guard lock(m_mutex);
      auto found = std::find_if(m_delayed.begin(), m_delayed.end(),
                                [identifier](const auto &entry) {
                                  return entry.identifier == identifier;
                                });
      if (found == m_delayed.end())
        return false;
      retired = std::move(found->item);
      m_delayed.erase(found);
    }
    return true;
  }
  bool RunOne(ClockTime now) noexcept {
    NGIN::Execution::detail::DispatchBudget budget;
    if (!budget.Entered())
      return false;
    struct Current {
      UIAsyncScheduler *previous;
      ~Current() { s_current = previous; }
    } current{s_current};
    s_current = this;
    for (unsigned attempt = 0; attempt != 3; ++attempt) {
      const auto source = m_nextSource;
      m_nextSource = (m_nextSource + 1) % 3;
      if (source == 0) {
        if (m_completions.RunOne()) {
          budget.Invoked();
          return true;
        }
      } else {
        NGIN::Execution::WorkItem work;
        {
          std::lock_guard lock(m_mutex);
          if (source == 1 && !m_ready.empty()) {
            work = std::move(m_ready.front());
            m_ready.pop_front();
          } else if (source == 2) {
            auto found = std::find_if(
                m_delayed.begin(), m_delayed.end(),
                [now](const auto &entry) { return entry.due <= now; });
            if (found != m_delayed.end()) {
              work = std::move(found->item);
              m_delayed.erase(found);
            }
          }
        }
        if (!work.IsEmpty()) {
          budget.Invoked();
          work.Invoke();
          return true;
        }
      }
    }
    return false;
  }
  static void Notify(void *context) noexcept {
    static_cast<UIAsyncScheduler *>(context)->Wake();
  }
  void Wake() const noexcept {
    if (m_wake)
      m_wake(m_platform);
  }
  const std::size_t m_capacity;
  NGIN::Execution::detail::CompletionQueue m_completions;
  mutable std::mutex m_mutex;
  std::deque<NGIN::Execution::WorkItem> m_ready;
  std::vector<DelayedEntry> m_delayed;
  std::uint64_t m_identifier{};
  unsigned m_nextSource{};
  bool m_closed{};
  void *m_platform{};
  ClockTime (*m_now)(void *) noexcept {};
  void (*m_wake)(void *) noexcept {};
  inline static thread_local UIAsyncScheduler *s_current{};
};
} // namespace NGIN::UI::Detail
