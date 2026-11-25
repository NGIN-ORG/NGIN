#pragma once

/// @file Tasks.hpp
/// @brief Task runtime lanes and scheduling contract.

#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Core/Types.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace NGIN::Core
{
    /// @brief Named execution lane.
    enum class TaskLane : NGIN::UInt8
    {
        Main,
        IO,
        Worker,
        Background,
        Render
    };

    /// @brief A callback executed by the scheduler. Callbacks must not throw.
    using TaskCallback = std::function<void()>;

    /// @brief Task runtime interface consumed by modules.
    class NGIN_CORE_API ITaskRuntime
    {
    public:
        virtual ~ITaskRuntime() = default;

        virtual auto Submit(TaskLane lane, TaskCallback callback) noexcept -> CoreResult<TaskId> = 0;
        virtual auto ScheduleAfter(TaskLane lane, std::chrono::milliseconds delay, TaskCallback callback) noexcept -> CoreResult<TaskId> = 0;
        /// @brief Wait for queued, running, and delayed callbacks in the lane.
        virtual auto Barrier(TaskLane lane) noexcept -> CoreResult<void> = 0;
        /// @brief Wait for queued, running, and delayed callbacks across all lanes.
        virtual auto BarrierAll() noexcept -> CoreResult<void> = 0;
        /// @brief Return false on timeout. A negative timeout waits indefinitely.
        virtual auto WaitIdle(std::chrono::milliseconds timeout) noexcept -> CoreResult<bool> = 0;
        [[nodiscard]] virtual auto IsLaneEnabled(TaskLane lane) const noexcept -> bool = 0;
    };

    /// @brief Create default task runtime.
    NGIN_CORE_API auto CreateTaskRuntime(NGIN::UInt32 workerThreads = 0, bool enableRenderLane = false) noexcept
        -> NGIN::Memory::Shared<ITaskRuntime>;

    /// @brief Default task runtime backed by lane-specific NGIN.Base schedulers.
    class NGIN_CORE_API TaskRuntime final : public ITaskRuntime
    {
    public:
        explicit TaskRuntime(NGIN::UInt32 workerThreads = 0, bool enableRenderLane = false);
        ~TaskRuntime() override;

        auto Submit(TaskLane lane, TaskCallback callback) noexcept -> CoreResult<TaskId> override;

        auto ScheduleAfter(TaskLane lane, std::chrono::milliseconds delay, TaskCallback callback) noexcept
            -> CoreResult<TaskId> override;

        auto Barrier(TaskLane lane) noexcept -> CoreResult<void> override;
        auto BarrierAll() noexcept -> CoreResult<void> override;
        auto WaitIdle(std::chrono::milliseconds timeout) noexcept -> CoreResult<bool> override;
        [[nodiscard]] auto IsLaneEnabled(TaskLane lane) const noexcept -> bool override;

    private:
        friend class KernelImpl;
        friend auto CreateTaskRuntime(NGIN::UInt32 workerThreads, bool enableRenderLane) noexcept
            -> NGIN::Memory::Shared<ITaskRuntime>;
        static constexpr std::size_t s_laneCount = 5;

        struct Activity
        {
            TaskRuntime& runtime;
            std::array<NGIN::UInt64, s_laneCount> lanes {};
            NGIN::UInt64 total {0};
        };

        class PendingTask;
        class ModuleTasks;

        [[nodiscard]] auto QueueTask(Activity& activity, TaskLane lane, std::chrono::milliseconds delay,
                                     TaskCallback callback) noexcept -> CoreResult<TaskId>;
        void FinishTask(Activity& activity, TaskLane lane) noexcept;
        void CloseAdmission() noexcept;
        [[nodiscard]] auto IsExecutingTask() const noexcept -> bool;
        [[nodiscard]] auto WaitForBarrier(const Activity& activity, std::optional<TaskLane> lane) noexcept
            -> CoreResult<void>;
        [[nodiscard]] auto WaitForIdle(const Activity& activity, std::chrono::milliseconds timeout,
                                       std::optional<TaskLane> lane = std::nullopt) noexcept -> CoreResult<bool>;

        TaskId m_nextId {1};
        bool m_acceptingTasks {true};
        Activity m_activity {*this};
        std::mutex m_idleMutex;
        std::condition_variable m_idleCv;

        std::array<std::unique_ptr<NGIN::Execution::ThreadPoolScheduler>, s_laneCount> m_schedulers;
    };
}
