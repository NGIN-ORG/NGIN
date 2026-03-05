#include <NGIN/Runtime/Tasks.hpp>

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>

#include <algorithm>

namespace NGIN::Runtime
{
    namespace
    {
        [[nodiscard]] constexpr auto LaneIndex(const TaskLane lane) noexcept -> std::size_t
        {
            return static_cast<std::size_t>(lane);
        }
    }

    TaskRuntime::TaskRuntime(const NGIN::UInt32 workerThreads, const bool enableRenderLane)
    {
        const auto hw = static_cast<std::size_t>(NGIN::Execution::ThisThread::HardwareConcurrency());
        const auto workerCount = workerThreads == 0 ? (hw == 0 ? 1u : static_cast<NGIN::UInt32>(hw)) : workerThreads;

        m_schedulers[LaneIndex(TaskLane::Main)] = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
        m_schedulers[LaneIndex(TaskLane::Worker)] = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(static_cast<std::size_t>(workerCount));
        m_schedulers[LaneIndex(TaskLane::IO)] = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(
            static_cast<std::size_t>(std::max<NGIN::UInt32>(1, workerCount / 2)));
        m_schedulers[LaneIndex(TaskLane::Background)] = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);

        if (enableRenderLane)
        {
            m_schedulers[LaneIndex(TaskLane::Render)] = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
        }
    }

    TaskRuntime::~TaskRuntime() = default;

    auto TaskRuntime::SchedulerForLane(const TaskLane lane) noexcept -> NGIN::Execution::ThreadPoolScheduler*
    {
        return m_schedulers[LaneIndex(lane)].get();
    }

    auto TaskRuntime::SchedulerForLane(const TaskLane lane) const noexcept -> const NGIN::Execution::ThreadPoolScheduler*
    {
        return m_schedulers[LaneIndex(lane)].get();
    }

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

    auto TaskRuntime::IsLaneEnabled(const TaskLane lane) const noexcept -> bool
    {
        return SchedulerForLane(lane) != nullptr;
    }

    auto TaskRuntime::Submit(const TaskLane lane, TaskCallback callback) noexcept -> RuntimeResult<TaskId>
    {
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task callback cannot be empty"));
        }

        auto* scheduler = SchedulerForLane(lane);
        if (!scheduler)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task lane is disabled"));
        }

        const TaskId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
        OnTaskBegin();

        try
        {
            scheduler->Execute(NGIN::Execution::WorkItem([this, callback = std::move(callback)]() mutable {
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

    auto TaskRuntime::ScheduleAfter(const TaskLane lane,
                                    const std::chrono::milliseconds delay,
                                    TaskCallback callback) noexcept -> RuntimeResult<TaskId>
    {
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "delayed callback cannot be empty"));
        }

        auto* scheduler = SchedulerForLane(lane);
        if (!scheduler)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task lane is disabled"));
        }

        const TaskId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
        OnTaskBegin();

        const auto now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
        const auto delayNs = static_cast<NGIN::UInt64>(delay.count() < 0 ? 0 : delay.count()) * 1'000'000ULL;
        const auto runAt = NGIN::Time::TimePoint::FromNanoseconds(now + delayNs);

        try
        {
            scheduler->ExecuteAt(
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

    auto TaskRuntime::Barrier(const TaskLane lane) noexcept -> RuntimeResult<void>
    {
        auto* scheduler = SchedulerForLane(lane);
        if (!scheduler)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task lane is disabled"));
        }

        scheduler->RunUntilIdle();
        return RuntimeResult<void> {};
    }

    auto TaskRuntime::BarrierAll() noexcept -> RuntimeResult<void>
    {
        for (const auto& scheduler : m_schedulers)
        {
            if (scheduler)
            {
                scheduler->RunUntilIdle();
            }
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

    auto CreateTaskRuntime(const NGIN::UInt32 workerThreads, const bool enableRenderLane) noexcept -> NGIN::Memory::Shared<ITaskRuntime>
    {
        return NGIN::Memory::MakeSharedAs<ITaskRuntime, TaskRuntime>(workerThreads, enableRenderLane);
    }
}
