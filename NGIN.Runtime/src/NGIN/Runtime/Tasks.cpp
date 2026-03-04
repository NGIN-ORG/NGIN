#include <NGIN/Runtime/Tasks.hpp>

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>

namespace NGIN::Runtime
{
    TaskRuntime::TaskRuntime(const NGIN::UInt32 threadCount)
        : m_scheduler(threadCount == 0 ? static_cast<std::size_t>(NGIN::Execution::ThisThread::HardwareConcurrency()) : static_cast<std::size_t>(threadCount))
    {
    }

    TaskRuntime::~TaskRuntime() = default;

    void TaskRuntime::OnTaskBegin() noexcept
    {
        m_activeTasks.fetch_add(1, std::memory_order_relaxed);
    }

    void TaskRuntime::OnTaskEnd() noexcept
    {
        if (m_activeTasks.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            std::lock_guard<std::mutex> lock(m_idleMutex);
            m_idleCv.notify_all();
        }
    }

    auto TaskRuntime::Submit(const TaskLane, TaskCallback callback) noexcept -> RuntimeResult<TaskId>
    {
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task callback cannot be empty"));
        }

        const TaskId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
        OnTaskBegin();

        try
        {
            m_scheduler.Execute(NGIN::Execution::WorkItem([this, callback = std::move(callback)]() mutable {
                callback();
                OnTaskEnd();
            }));
        }
        catch (...)
        {
            OnTaskEnd();
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::TaskSubmissionFailure, "Tasks", {}, "failed to submit task"));
        }

        return id;
    }

    auto TaskRuntime::ScheduleAfter(const TaskLane,
                                    const std::chrono::milliseconds delay,
                                    TaskCallback callback) noexcept -> RuntimeResult<TaskId>
    {
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "delayed callback cannot be empty"));
        }

        const TaskId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
        OnTaskBegin();

        const auto now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
        const auto delayNs = static_cast<NGIN::UInt64>(delay.count() < 0 ? 0 : delay.count()) * 1'000'000ULL;
        const auto runAt = NGIN::Time::TimePoint::FromNanoseconds(now + delayNs);

        try
        {
            m_scheduler.ExecuteAt(
                NGIN::Execution::WorkItem([this, callback = std::move(callback)]() mutable {
                    callback();
                    OnTaskEnd();
                }),
                runAt);
        }
        catch (...)
        {
            OnTaskEnd();
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::TaskSubmissionFailure, "Tasks", {}, "failed to schedule delayed task"));
        }

        return id;
    }

    auto TaskRuntime::Barrier() noexcept -> RuntimeResult<void>
    {
        while (m_scheduler.RunOne())
        {
        }
        return RuntimeResult<void> {};
    }

    auto TaskRuntime::WaitIdle(const std::chrono::milliseconds timeout) noexcept -> RuntimeResult<bool>
    {
        std::unique_lock<std::mutex> lock(m_idleMutex);
        if (timeout.count() < 0)
        {
            m_idleCv.wait(lock, [&]() { return m_activeTasks.load(std::memory_order_acquire) == 0; });
            return true;
        }

        const auto success = m_idleCv.wait_for(
            lock,
            timeout,
            [&]() { return m_activeTasks.load(std::memory_order_acquire) == 0; });
        return success;
    }

    auto CreateTaskRuntime(const NGIN::UInt32 threadCount) noexcept -> NGIN::Memory::Shared<ITaskRuntime>
    {
        return NGIN::Memory::MakeSharedAs<ITaskRuntime, TaskRuntime>(threadCount);
    }
}

