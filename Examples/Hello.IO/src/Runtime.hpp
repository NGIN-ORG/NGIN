#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/FileSystemDriver.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#include <memory>
#include <utility>

namespace HelloIO
{
    // Application-owned services, shared by all demos. This is example code,
    // not a second runtime abstraction that an application must adopt.
    class Runtime final
    {
    public:
        Runtime()
        {
            NGIN::Execution::Thread::Options options;
            options.name = NGIN::Execution::ThreadName("Hello.IO.Net");
            m_networkThread.Start([this] { m_network->Run(); }, options);
        }

        ~Runtime()
        {
            // The caller has already awaited every operation, including canceled
            // operations. Stop() ends polling; it does not drain application tasks.
            m_network->Stop();
            if (m_networkThread.IsJoinable())
            {
                m_networkThread.Join();
            }
        }

        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;

        NGIN::Async::TaskContext
        MakeTaskContext(NGIN::Async::CancellationToken token = {})
        {
            return NGIN::Async::TaskContext(m_tasks, std::move(token));
        }

        NGIN::IO::LocalFileSystem&                Files() noexcept { return m_files; }
        NGIN::Net::NetworkDriver&                 Network() noexcept { return *m_network; }
        NGIN::IO::FileSystemDriver::ActiveBackend FileBackend() const noexcept
        {
            return m_fileDriver->GetActiveBackend();
        }

    private:
        // Declaration order matters: destruction runs in reverse. The task
        // scheduler outlives both I/O drivers and their completion submissions.
        // One task worker is enough for these mostly-waiting coroutines.
        NGIN::Execution::ThreadPoolScheduler        m_tasks {1};
        std::shared_ptr<NGIN::IO::FileSystemDriver> m_fileDriver =
                std::make_shared<NGIN::IO::FileSystemDriver>();
        NGIN::IO::LocalFileSystem m_files {m_fileDriver};
        // Zero internal workers: our dedicated thread performs the Run() loop.
        std::unique_ptr<NGIN::Net::NetworkDriver> m_network =
                NGIN::Net::NetworkDriver::Create({.workerThreads = 0});
        NGIN::Execution::Thread m_networkThread;
    };
}// namespace HelloIO
