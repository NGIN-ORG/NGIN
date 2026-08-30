---
title: WorkItem and ScheduleResult
description: Code reference for move-only scheduled work and submission errors in NGIN.Execution.
---

# `WorkItem` and `ScheduleResult`

**Headers:** `<NGIN/Execution/WorkItem.hpp>`, `<NGIN/Execution/ScheduleResult.hpp>`  
**Namespace:** `NGIN::Execution`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`WorkItem.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/WorkItem.hpp#L61)

## WorkItem declaration

```cpp
class WorkItem final {
public:
    enum class Kind : unsigned char { Empty, Coroutine, Job };

    constexpr WorkItem() noexcept;
    explicit WorkItem(std::coroutine_handle<> coroutine) noexcept;
    explicit WorkItem(Utilities::Callable<void()> job);

    template<typename F>
    explicit WorkItem(F&& job);

    WorkItem(WorkItem&&) noexcept;
    WorkItem& operator=(WorkItem&&) noexcept;
    WorkItem(const WorkItem&) = delete;

    [[nodiscard]] Kind GetKind() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] bool IsCoroutine() const noexcept;
    [[nodiscard]] bool IsJob() const noexcept;
    [[nodiscard]] std::coroutine_handle<> GetCoroutine() const noexcept;
    void Invoke() noexcept;
};
```

The item uniquely owns job callable storage or borrows the lifetime contract of
the supplied coroutine handle. Moving empties the source. Small nothrow-movable
jobs use inline storage; larger jobs allocate.

`Invoke` resumes a non-complete coroutine or calls a job. Any exception escaping
job invocation terminates because `Invoke` is `noexcept`.

## Submission result

```cpp
enum class ScheduleError : std::uint8_t {
    InvalidExecutor,
    Rejected,
    Stopped,
    ResourceExhausted,
};

using ScheduleResult = std::expected<void, ScheduleError>;
```

| Error | Contract |
| --- | --- |
| `InvalidExecutor` | A borrowed executor has no target |
| `Rejected` | Work is empty/invalid or policy rejected it |
| `Stopped` | Scheduler shutdown prevents acceptance |
| `ResourceExhausted` | Required queue/callable allocation failed |

## Preconditions

- A direct callable constructor requires a non-empty callable.
- A work item is single-owner and must be moved into submission.
- Do not invoke the same coroutine handle through multiple work items.
- Captures and coroutine state must remain valid for their execution contract.

