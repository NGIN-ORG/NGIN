#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/Runtime.hpp>

#include <utility>

namespace HelloIO
{
    // Composition at the application boundary: tasks choose their executor,
    // while filesystem and sockets share the lazily initialized I/O runtime.
    class Application final
    {
    public:
        NGIN::Async::TaskContext MakeTaskContext(NGIN::Async::CancellationToken token = {})
        {
            return NGIN::Async::TaskContext(m_tasks, std::move(token));
        }
        NGIN::IO::LocalFileSystem& Files() noexcept { return m_files; }
        NGIN::IO::Runtime&         Io() noexcept { return m_io; }

    private:
        // Callers await all tasks before destruction. Reverse destruction order
        // releases the filesystem, stops/joins I/O, then destroys the task executor.
        NGIN::Execution::ThreadPoolScheduler m_tasks {1};
        NGIN::IO::Runtime                    m_io;
        NGIN::IO::LocalFileSystem            m_files {m_io};
    };
}// namespace HelloIO
