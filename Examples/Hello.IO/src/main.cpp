// SocketHandle currently has no local-endpoint query. Keep the small native
// getsockname bridge here so the listener can use an OS-selected port.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include "Runtime.hpp"

#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Transport/Filters/LengthPrefixedMessageStream.hpp>
#include <NGIN/Net/Transport/TcpByteStream.hpp>

#include <array>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace HelloIO
{
    namespace Async = NGIN::Async;
    namespace IO    = NGIN::IO;
    namespace Net   = NGIN::Net;
    using NGIN::Units::Milliseconds;
    using MessageStream = Net::Transport::Filters::LengthPrefixedMessageStream;

    void Expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            throw std::runtime_error(std::string(message));
        }
    }

    std::string Describe(const IO::IOError& error)
    {
        return "IO code " + std::to_string(static_cast<int>(error.code)) +
               ", native " + std::to_string(error.systemCode) + ": " +
               std::string(error.message.Data(), error.message.Size());
    }

    std::string Describe(const Net::NetError& error)
    {
        return Net::ToErrorCode(error).message() + " (native " +
               std::to_string(error.native) + ")";
    }

    // Synchronous setup failures are reported at the application boundary.
    template<typename T, typename E>
    T Require(NGIN::Utilities::Expected<T, E> result, std::string_view action)
    {
        if (!result)
        {
            throw std::runtime_error(std::string(action) + ": " +
                                     Describe(result.error()));
        }
        if constexpr (!std::is_void_v<T>)
        {
            return std::move(result).value();
        }
    }

    // Completion distinguishes typed I/O errors, cancellation, and runtime faults.
    template<typename T, typename E>
    T Require(Async::Completion<T, E> result, std::string_view action)
    {
        const std::string prefix = std::string(action) + ": ";
        if (result.IsCanceled())
        {
            throw std::runtime_error(prefix +
                                     "canceled (application deadline reached)");
        }
        if (result.IsFault())
        {
            throw std::runtime_error(
                    prefix + "async fault " +
                    std::to_string(static_cast<int>(result.Fault().code)) + ": " +
                    result.Fault().message);
        }
        if constexpr (!std::is_same_v<E, Async::NoError>)
        {
            if (result.IsDomainError())
            {
                throw std::runtime_error(prefix + Describe(result.DomainError()));
            }
        }
        if constexpr (!std::is_void_v<T>)
        {
            return std::move(result).Value();
        }
    }

    class ScratchDirectory final
    {
    public:
        explicit ScratchDirectory(IO::LocalFileSystem& files)
            : m_files(files),
              m_path(Require(files.CreateTempDirectory({}, "hello_io_"),
                             "create scratch directory")) {}

        ~ScratchDirectory()
        {
            // Best-effort cleanup on failure; Remove() checks cleanup on success.
            if (!m_removed)
            {
                auto result = m_files.RemoveDirectory(
                        m_path, {.recursive = true, .ignoreMissing = true});
                if (!result)
                {
                    std::cerr << "Scratch cleanup failed: " << Describe(result.error())
                              << '\n';
                }
            }
        }

        ScratchDirectory(const ScratchDirectory&)            = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        const IO::Path& Path() const noexcept { return m_path; }

        void Remove()
        {
            Require(m_files.RemoveDirectory(m_path, {.recursive = true}),
                    "remove scratch directory");
            m_removed = true;
        }

    private:
        IO::LocalFileSystem& m_files;
        IO::Path             m_path;
        bool                 m_removed {false};
    };

    Net::ConstByteSpan Bytes(std::string_view text)
    {
        return {reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()};
    }

    std::string Text(Net::ConstByteSpan bytes)
    {
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    const char* BackendName(IO::FileSystemDriver::ActiveBackend backend)
    {
        using Backend = IO::FileSystemDriver::ActiveBackend;
        switch (backend)
        {
            case Backend::NativeIoUring:
                return "io_uring";
            case Backend::NativeIocp:
                return "IOCP";
            case Backend::WorkerFallback:
                return "worker fallback";
            case Backend::None:
                return "unavailable";
        }
        return "unknown";
    }

    Async::Task<std::string> ReadSensor(Async::TaskContext& ctx,
                                        std::string reading, Milliseconds delay)
    {
        // Simulated sensors: timers suspend the coroutine without sleeping a worker.
        co_await ctx.Delay(delay);
        co_return reading;
    }

    Async::Task<std::string> GatherTelemetry(Async::TaskContext& ctx)
    {
        // WhenAll starts both cold tasks and waits for both, including failures.
        auto [shields, snacks] = co_await Async::WhenAll(
                ctx, ReadSensor(ctx, "Shields: 98%", Milliseconds {40}),
                ReadSensor(ctx, "Emergency cookies: 7", Milliseconds {70}));
        co_return "Captain's log: the async expedition\n" + shields + "\n" + snacks +
                "\n";
    }

    Async::Task<std::string, IO::IOError> SaveMissionLog(Async::TaskContext&  ctx,
                                                         IO::LocalFileSystem& files,
                                                         IO::Path             directory,
                                                         std::string          log)
    {
        const auto original = directory.Join("mission.txt");
        const auto backup   = directory.Join("black-box.txt");

        // The coroutine frame owns log while WriteAllBytesAsync borrows its bytes.
        co_await IO::WriteAllBytesAsync(files, ctx, original, Bytes(log));
        co_await IO::CopyFileAsync(files, ctx, original, backup);
        auto       restored     = co_await IO::ReadAllBytesAsync(files, ctx, backup);
        const auto restoredText = Text({restored.data(), restored.Size()});
        Expect(restoredText == log, "black-box log did not match the original");
        co_return restoredText;
    }

    NGIN::UInt16 BoundPort(const Net::SocketHandle& handle)
    {
        sockaddr_in address {};
#if defined(_WIN32)
        int length = sizeof(address);
        if (::getsockname(static_cast<SOCKET>(handle.Native()),
                          reinterpret_cast<sockaddr*>(&address), &length) != 0)
        {
            throw std::system_error(WSAGetLastError(), std::system_category(),
                                    "query loopback port");
        }
#else
        socklen_t length = sizeof(address);
        if (::getsockname(static_cast<int>(handle.Native()),
                          reinterpret_cast<sockaddr*>(&address), &length) != 0)
        {
            throw std::system_error(errno, std::system_category(),
                                    "query loopback port");
        }
#endif
        const auto port = ntohs(address.sin_port);
        Expect(port != 0, "listener did not receive an ephemeral port");
        return port;
    }

    Net::TcpListener OpenStation()
    {
        Net::TcpListener listener;
        Require(listener.Open(Net::AddressFamily::V4), "open station listener");
        Require(listener.Bind({Net::IpAddress::LoopbackV4(), 0}),
                "bind loopback station");
        Require(listener.Listen(), "listen for probe");
        return listener;
    }

    Async::Task<void, Net::NetError> EchoStation(Async::TaskContext& ctx,
                                                 Net::NetworkDriver& driver,
                                                 Net::TcpListener&   listener)
    {
        const auto token  = ctx.GetCancellationToken();
        auto       socket = co_await listener.AcceptAsync(ctx, driver, token);
        // This existing adapter owns the socket and borrows the shared driver once.
        MessageStream                messages(std::make_unique<Net::Transport::TcpByteStream>(
                std::move(socket), driver));
        std::array<NGIN::Byte, 4096> storage {};
        Net::Buffer                  buffer;
        buffer.data     = storage.data();
        buffer.capacity = static_cast<NGIN::UInt32>(storage.size());

        // TCP has no message boundaries. The framing adapter handles partial
        // reads/writes and refuses a message larger than our supplied buffer.
        auto message = co_await messages.ReadMessageAsync(ctx, buffer, token);
        co_await messages.WriteMessageAsync(ctx, message, token);
        co_return;
    }

    Async::Task<void, Net::NetError>
    SendProbeLog(Async::TaskContext& ctx, Net::NetworkDriver& driver,
                 Net::TcpSocket socket, Net::Endpoint station, std::string log)
    {
        const auto token = ctx.GetCancellationToken();
        co_await socket.ConnectAsync(ctx, driver, station, token);
        MessageStream messages(std::make_unique<Net::Transport::TcpByteStream>(
                std::move(socket), driver));
        co_await messages.WriteMessageAsync(ctx, Bytes(log), token);

        std::array<NGIN::Byte, 4096> storage {};
        Net::Buffer                  reply;
        reply.data     = storage.data();
        reply.capacity = static_cast<NGIN::UInt32>(storage.size());
        auto echoed    = co_await messages.ReadMessageAsync(ctx, reply, token);
        Expect(Text(echoed) == log, "mission control echoed a different log");
        co_return;
    }

    void ExchangeWithMissionControl(Runtime& runtime, Async::TaskContext& ctx,
                                    const std::string& log)
    {
        auto           listener = OpenStation();
        Net::TcpSocket probe;
        Require(probe.Open(Net::AddressFamily::V4), "open probe socket");
        const Net::Endpoint station {Net::IpAddress::LoopbackV4(),
                                     BoundPort(listener.Handle())};

        // All children finish before listener, driver references, or buffers go
        // away. The application deadline also releases a peer left waiting if
        // the other child fails. Never block a task worker with SyncWait.
        Require(Async::SyncWait(
                        ctx,
                        Async::WhenAll(ctx, EchoStation(ctx, runtime.Network(), listener),
                                       SendProbeLog(ctx, runtime.Network(),
                                                    std::move(probe), station, log))),
                "exchange mission log");
    }

    void CancelSilentStation(Runtime&            runtime,
                             Async::TaskContext& applicationContext)
    {
        auto                      listener = OpenStation();
        Async::CancellationSource patience;
        auto                      waitContext =
                applicationContext.WithLinkedCancellationToken(patience.GetToken());
        Expect(patience.CancelAfter(waitContext.GetExecutor(), Milliseconds {100})
                       .has_value(),
               "could not schedule station timeout");

        // No client will connect. Cancellation requests termination; awaiting the
        // terminal completion is what makes it safe to destroy the listener.
        auto result = Async::SyncWait(
                waitContext, listener.AcceptAsync(waitContext, runtime.Network(),
                                                  waitContext.GetCancellationToken()));
        if (!result.IsCanceled())
        {
            Require(std::move(result), "wait for silent station");
            throw std::runtime_error(
                    "silent station unexpectedly accepted a connection");
        }
        Expect(!applicationContext.IsCancellationRequested(),
               "application deadline reached during cancellation demo");
    }
}// namespace HelloIO

int main()
{
    using namespace HelloIO;
    try
    {
        Runtime                   runtime;
        Async::CancellationSource shutdown;
        auto                      ctx = runtime.MakeTaskContext(shutdown.GetToken());
        Expect(shutdown.CancelAfter(ctx.GetExecutor(), Milliseconds {10000})
                       .has_value(),
               "could not schedule application deadline");
        ScratchDirectory scratch(runtime.Files());

        std::cout << "Hello.IO: launching the async expedition\n"
                  << "File backend: " << BackendName(runtime.FileBackend()) << '\n';
        auto log =
                Require(Async::SyncWait(ctx, GatherTelemetry(ctx)), "gather telemetry");
        std::cout << "[async] Both sensors checked in\n"
                  << log;

        auto restored =
                Require(Async::SyncWait(ctx, SaveMissionLog(ctx, runtime.Files(),
                                                            scratch.Path(), log)),
                        "save and restore black-box log");
        std::cout << "[files] Mission log saved, copied, and verified\n";

        ExchangeWithMissionControl(runtime, ctx, restored);
        std::cout << "[tcp] Mission control echoed the complete log\n";

        CancelSilentStation(runtime, ctx);
        std::cout << "[cancel] Silent station wait canceled and completed\n";

        // Every root and child operation is terminal before cleanup and Stop().
        shutdown.Cancel();
        scratch.Remove();
        std::cout << "Hello.IO complete: scratch files removed, all tasks joined\n";
        return 0;
    } catch (const std::exception& error)
    {
        std::cerr << "Hello.IO failed: " << error.what() << '\n';
        return 1;
    }
}
