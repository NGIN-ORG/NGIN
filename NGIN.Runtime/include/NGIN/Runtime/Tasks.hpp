#pragma once

/// @file Tasks.hpp
/// @brief Task runtime lanes and scheduling contract.

#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Export.hpp>
#include <NGIN/Runtime/Types.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace NGIN::Runtime
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

    using TaskCallback = std::function<void()>;

    /// @brief Task runtime interface consumed by modules.
    class NGIN_RUNTIME_API ITaskRuntime
    {
    public:
        virtual ~ITaskRuntime() = default;

        virtual auto Submit(TaskLane lane, TaskCallback callback) noexcept -> RuntimeResult<TaskId> = 0;
        virtual auto ScheduleAfter(TaskLane lane, std::chrono::milliseconds delay, TaskCallback callback) noexcept -> RuntimeResult<TaskId> = 0;
        virtual auto Barrier() noexcept -> RuntimeResult<void> = 0;
        virtual auto WaitIdle(std::chrono::milliseconds timeout) noexcept -> RuntimeResult<bool> = 0;
    };

    /// @brief Default task runtime backed by NGIN.Base thread-pool scheduler.
    class NGIN_RUNTIME_API TaskRuntime final : public ITaskRuntime
    {
    public:
        explicit TaskRuntime(NGIN::UInt32 threadCount = 0);
        ~TaskRuntime() override;

        auto Submit(TaskLane lane, TaskCallback callback) noexcept -> RuntimeResult<TaskId> override;

        auto ScheduleAfter(TaskLane lane, std::chrono::milliseconds delay, TaskCallback callback) noexcept
            -> RuntimeResult<TaskId> override;

        auto Barrier() noexcept -> RuntimeResult<void> override;
        auto WaitIdle(std::chrono::milliseconds timeout) noexcept -> RuntimeResult<bool> override;

    private:
        void OnTaskBegin() noexcept;
        void OnTaskEnd() noexcept;

        std::atomic<NGIN::UInt64>          m_nextId {1};
        std::atomic<NGIN::UInt64>          m_activeTasks {0};
        std::mutex                         m_idleMutex;
        std::condition_variable            m_idleCv;
        NGIN::Execution::ThreadPoolScheduler m_scheduler;
    };

    /// @brief Create default task runtime.
    NGIN_RUNTIME_API auto CreateTaskRuntime(NGIN::UInt32 threadCount = 0) noexcept -> NGIN::Memory::Shared<ITaskRuntime>;
}
