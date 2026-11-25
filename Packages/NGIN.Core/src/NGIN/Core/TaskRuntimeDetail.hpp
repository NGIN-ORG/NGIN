#pragma once

#include <NGIN/Core/Tasks.hpp>

#include <thread>
#include <utility>

namespace NGIN::Core
{
    class TaskRuntime::ModuleTasks final : public ITaskRuntime
    {
    public:
        explicit ModuleTasks(TaskRuntime& runtime) noexcept : m_activity {runtime} {}

        auto Submit(TaskLane lane, TaskCallback callback) noexcept -> CoreResult<TaskId> override;
        auto ScheduleAfter(TaskLane lane, std::chrono::milliseconds delay, TaskCallback callback) noexcept
            -> CoreResult<TaskId> override;
        auto Barrier(TaskLane lane) noexcept -> CoreResult<void> override;
        auto BarrierAll() noexcept -> CoreResult<void> override;
        auto WaitIdle(std::chrono::milliseconds timeout) noexcept -> CoreResult<bool> override;
        [[nodiscard]] auto IsLaneEnabled(TaskLane lane) const noexcept -> bool override;

    private:
        Activity m_activity;
    };

    template<typename Interface, typename Object, typename... Args>
    auto MakeTaskAwareShared(bool (Object::*isExecutingTask)() const noexcept, Args&&... args)
        -> NGIN::Memory::Shared<Interface>
    {
        const auto destroy = [isExecutingTask](Object* object) noexcept
        {
            if ((object->*isExecutingTask)())
            {
                // The reaper owns the object until its task thread can be joined.
                // Failure to start the reaper is fatal: deleting here would join
                // the current thread and leave the runtime in use by its worker.
                std::thread([object]() { delete object; }).detach();
            }
            else
            {
                delete object;
            }
        };
        auto object = std::unique_ptr<Object, decltype(destroy)>(
            new Object(std::forward<Args>(args)...), destroy);
        return NGIN::Memory::MakeSharedAlias<Interface>(object.get(), std::move(object));
    }
}
