#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/Runtime.hpp>

#include <utility>

namespace HelloIO
{
    // Binding resources does not choose a driver thread or continuation executor.
    class Application final
    {
    public:
        NGIN::Async::TaskContext
        MakeTaskContext(NGIN::Async::CancellationToken token = {})
        {
            return NGIN::Async::TaskContext(m_io.GetExecutor(), std::move(token));
        }
        NGIN::IO::LocalFileSystem& Files() noexcept { return m_files; }
        NGIN::IO::Runtime&         Io() noexcept { return m_io; }

    private:
        // The caller joins application tasks and shuts down before these owners
        // leave scope. Files are destroyed before their borrowed runtime.
        NGIN::IO::Runtime         m_io;
        NGIN::IO::LocalFileSystem m_files {m_io};
    };
}// namespace HelloIO
