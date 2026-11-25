#include "TaskRuntimeDetail.hpp"

#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace NGIN::Core
{
    namespace
    {
        thread_local const TaskRuntime* executingRuntime = nullptr;
        thread_local TaskLane executingLane = TaskLane::Main;

        [[nodiscard]] constexpr auto LaneIndex(const TaskLane lane) noexcept -> std::size_t
        {
            return static_cast<std::size_t>(lane);
        }
    }

    // The scheduler owns this object after submission. A task remains active
    // until its callback and captures are gone, including on rejection or discard.
    class TaskRuntime::PendingTask
    {
    public:
        PendingTask(Activity& activity, TaskLane lane, TaskCallback callback) noexcept
            : m_callback(std::move(callback)), m_activity(&activity), m_lane(lane)
        {
        }

        PendingTask(PendingTask&& other) noexcept
            : m_callback(std::move(other.m_callback)),
              m_activity(std::exchange(other.m_activity, nullptr)), m_lane(other.m_lane)
        {
        }

        ~PendingTask()
        {
            Finish();
        }

        void operator()() noexcept
        {
            const auto* previousRuntime = std::exchange(executingRuntime, &m_activity->runtime);
            const auto previousLane = std::exchange(executingLane, m_lane);
            m_callback();
            Finish();
            executingLane = previousLane;
            executingRuntime = previousRuntime;
        }

    private:
        void Finish() noexcept
        {
            if (auto* activity = std::exchange(m_activity, nullptr))
            {
                m_callback = nullptr;
                activity->runtime.FinishTask(*activity, m_lane);
            }
        }

        TaskCallback m_callback;
        Activity* m_activity;
        TaskLane m_lane;
    };

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

    void TaskRuntime::FinishTask(Activity& activity, const TaskLane lane) noexcept
    {
        std::lock_guard lock(m_idleMutex);
        const auto laneIndex = LaneIndex(lane);
        auto laneBecameIdle = --m_activity.lanes[laneIndex] == 0;
        --m_activity.total;

        if (&activity != &m_activity)
        {
            --activity.total;
            const auto ownerLaneIdle = --activity.lanes[laneIndex] == 0;
            laneBecameIdle = laneBecameIdle || ownerLaneIdle;
        }
        if (laneBecameIdle)
        {
            m_idleCv.notify_all();
        }
    }

    void TaskRuntime::CloseAdmission() noexcept
    {
        std::lock_guard lock(m_idleMutex);
        m_acceptingTasks = false;
    }

    auto TaskRuntime::IsExecutingTask() const noexcept -> bool
    {
        return executingRuntime == this;
    }

    auto TaskRuntime::IsLaneEnabled(const TaskLane lane) const noexcept -> bool
    {
        const auto index = LaneIndex(lane);
        return index < m_schedulers.size() && m_schedulers[index] != nullptr;
    }

    auto TaskRuntime::QueueTask(Activity& activity, const TaskLane lane,
                                const std::chrono::milliseconds delay,
                                TaskCallback callback) noexcept -> CoreResult<TaskId>
    {
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task callback cannot be empty"));
        }

        if (!IsLaneEnabled(lane))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task lane is disabled"));
        }

        NGIN::Time::TimePoint runAt;
        const bool delayed = delay.count() > 0;
        if (delayed)
        {
            constexpr NGIN::UInt64 nanosecondsPerMillisecond = 1'000'000;
            const auto now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            const auto delayMs = static_cast<NGIN::UInt64>(delay.count());
            if (delayMs > ((std::numeric_limits<NGIN::UInt64>::max)() - now) / nanosecondsPerMillisecond)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task delay is too large"));
            }
            runAt = NGIN::Time::TimePoint::FromNanoseconds(now + delayMs * nanosecondsPerMillisecond);
        }

        const auto laneIndex = LaneIndex(lane);
        TaskId id;
        {
            std::lock_guard lock(m_idleMutex);
            if (!m_acceptingTasks)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidState, "Tasks", {}, "task runtime is stopping"));
            }
            id = m_nextId++;
            ++m_activity.total;
            ++m_activity.lanes[laneIndex];
            if (&activity != &m_activity)
            {
                ++activity.total;
                ++activity.lanes[laneIndex];
            }
        }

        try
        {
            NGIN::Execution::WorkItem work(PendingTask(activity, lane, std::move(callback)));
            auto& scheduler = *m_schedulers[laneIndex];
            const auto submission = delayed ? scheduler.ExecuteAt(std::move(work), runAt)
                                            : scheduler.Execute(std::move(work));
            if (submission)
            {
                return id;
            }
        }
        catch (...)
        {
            // Constructing the work item can allocate. Its pending task owns
            // the accounting so a failed allocation also retires the task.
        }

        return NGIN::Utilities::Unexpected<KernelError>(
            MakeKernelError(KernelErrorCode::TaskSubmissionFailure, "Tasks", {},
                            delayed ? "failed to schedule delayed task" : "failed to submit task"));
    }

    auto TaskRuntime::Submit(const TaskLane lane, TaskCallback callback) noexcept -> CoreResult<TaskId>
    {
        return QueueTask(m_activity, lane, {}, std::move(callback));
    }

    auto TaskRuntime::ScheduleAfter(const TaskLane lane, const std::chrono::milliseconds delay,
                                    TaskCallback callback) noexcept -> CoreResult<TaskId>
    {
        return QueueTask(m_activity, lane, delay, std::move(callback));
    }

    auto TaskRuntime::WaitForBarrier(const Activity& activity, const std::optional<TaskLane> lane) noexcept
        -> CoreResult<void>
    {
        if (lane && !IsLaneEnabled(*lane))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Tasks", {}, "task lane is disabled"));
        }

        const auto idle = WaitForIdle(activity, std::chrono::milliseconds(-1), lane);
        if (!idle)
        {
            return NGIN::Utilities::Unexpected<KernelError>(idle.error());
        }
        return {};
    }

    auto TaskRuntime::Barrier(const TaskLane lane) noexcept -> CoreResult<void>
    {
        return WaitForBarrier(m_activity, lane);
    }

    auto TaskRuntime::BarrierAll() noexcept -> CoreResult<void>
    {
        return WaitForBarrier(m_activity, std::nullopt);
    }

    auto TaskRuntime::WaitForIdle(const Activity& activity, const std::chrono::milliseconds timeout,
                                  const std::optional<TaskLane> lane) noexcept -> CoreResult<bool>
    {
        std::unique_lock lock(m_idleMutex);
        const auto idle = [&]()
        {
            return (lane ? activity.lanes[LaneIndex(*lane)] : activity.total) == 0;
        };
        if (idle() || timeout.count() == 0)
        {
            return idle();
        }
        if (IsExecutingTask() && (!lane || *lane == executingLane))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidState, "Tasks", {}, "task cannot wait for its own lane to drain"));
        }
        if (timeout.count() < 0)
        {
            m_idleCv.wait(lock, idle);
            return true;
        }
        return m_idleCv.wait_for(lock, timeout, idle);
    }

    auto TaskRuntime::WaitIdle(const std::chrono::milliseconds timeout) noexcept -> CoreResult<bool>
    {
        return WaitForIdle(m_activity, timeout);
    }

    auto TaskRuntime::ModuleTasks::Submit(const TaskLane lane, TaskCallback callback) noexcept -> CoreResult<TaskId>
    {
        return m_activity.runtime.QueueTask(m_activity, lane, {}, std::move(callback));
    }

    auto TaskRuntime::ModuleTasks::ScheduleAfter(const TaskLane lane, const std::chrono::milliseconds delay,
                                                 TaskCallback callback) noexcept -> CoreResult<TaskId>
    {
        return m_activity.runtime.QueueTask(m_activity, lane, delay, std::move(callback));
    }

    auto TaskRuntime::ModuleTasks::Barrier(const TaskLane lane) noexcept -> CoreResult<void>
    {
        return m_activity.runtime.WaitForBarrier(m_activity, lane);
    }

    auto TaskRuntime::ModuleTasks::BarrierAll() noexcept -> CoreResult<void>
    {
        return m_activity.runtime.WaitForBarrier(m_activity, std::nullopt);
    }

    auto TaskRuntime::ModuleTasks::WaitIdle(const std::chrono::milliseconds timeout) noexcept -> CoreResult<bool>
    {
        return m_activity.runtime.WaitForIdle(m_activity, timeout);
    }

    auto TaskRuntime::ModuleTasks::IsLaneEnabled(const TaskLane lane) const noexcept -> bool
    {
        return m_activity.runtime.IsLaneEnabled(lane);
    }

    auto CreateTaskRuntime(const NGIN::UInt32 workerThreads, const bool enableRenderLane) noexcept -> NGIN::Memory::Shared<ITaskRuntime>
    {
        try
        {
            return MakeTaskAwareShared<ITaskRuntime>(&TaskRuntime::IsExecutingTask, workerThreads, enableRenderLane);
        }
        catch (...)
        {
            return {};
        }
    }
}
