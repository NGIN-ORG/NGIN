# Hello.IO

A small space expedition demonstrating application setup for `NGIN.Base` async
tasks, filesystem I/O, and networking. It runs entirely on your machine, with
an OS-selected loopback TCP port and a uniquely created temporary directory.
No server, Internet connection, or `NGIN.Core` host is required.

## Run it

From the repository root, with the built `ngin` CLI on `PATH`:

```bash
ngin validate --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug
ngin build --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
ngin run --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
ngin test --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
```

Alternatively, use `./build/dev/Tools/NGIN.CLI/ngin` (`ngin.exe` on Windows).
See the [repository setup instructions](../../README.md#try-ngin) to build it.

The executable checks its own results and returns nonzero on failure. The Test
registration runs the same demos with a 20-second process timeout; application
operations share a cooperative 10-second cancellation deadline.

Typical output (the selected file backend varies by platform):

```text
Hello.IO: launching the async expedition
[async] Both sensors checked in
Captain's log: the async expedition
Shields: 98%
Emergency cookies: 7
File backend: worker fallback
[files] Mission log saved, copied, and verified
[tcp] Mission control echoed the complete log
[cancel] Silent station wait canceled and completed
Hello.IO complete: scratch files removed, all tasks joined
```

## Application setup

Start with [`src/Application.hpp`](src/Application.hpp). The application owns
one task scheduler and one `NGIN::IO::Runtime`:

| Resource | Responsibility |
| --- | --- |
| `ThreadPoolScheduler` | Executes coroutine continuations and timers; accepts completion submissions from I/O threads |
| `TaskContext` | Borrows that scheduler and carries cancellation into tasks |
| `IO::Runtime` | Lazily owns file and network backends, including the background network thread |
| `LocalFileSystem(io)` | Binds file operations to the shared runtime |
| `TcpSocket(io)` / `TcpListener(io)` | Bind socket operations once; accepted sockets inherit the listener binding |

Construction and binding start no I/O workers. The first async file operation
initializes the file backend; the first async socket operation initializes the
network backend. A networking-only application starts no filesystem workers.
The task scheduler uses one worker for these mostly-waiting coroutines and has
its own timer infrastructure. TaskContext remains independent of the I/O runtime.

All demos use the same application task context. The main thread calls
`SyncWait` only at application boundaries. Coroutine functions use `co_await`;
blocking an executor worker could prevent progress. `ThreadPoolScheduler`
accepts cross-thread completion submissions; the current unsynchronized
`CooperativeScheduler` cannot be substituted into this background-I/O setup.

`IO::Runtime` defaults to Background networking. Applications with their own
polling loop can select `NetworkMode::Manual` and call `Run()` or `PollOnce()`
on one polling thread. Background mode requires neither call and rejects them.

## The expedition

Read the named functions in [`src/main.cpp`](src/main.cpp):

1. **Gather telemetry.** Two simulated sensors await different timers.
   `WhenAll` starts both tasks and joins them. Timers suspend tasks without
   sleeping a worker thread.
2. **Save the black box.** Write the telemetry asynchronously, copy it to a
   backup, read it back, and verify every byte. The coroutine owns the string
   while the write operation borrows its byte span. `LocalFileSystem` uses
   the shared runtime's native backend when available, otherwise its fallback.
3. **Contact mission control.** Run a TCP listener and client concurrently on
   loopback. Send the saved log and verify the echo. `TcpByteStream` owns its
   socket and preserves its runtime binding; `LengthPrefixedMessageStream` handles
   message framing and partial TCP transfers. Receive storage is bounded to
   4096 bytes and stays alive across every await.
4. **Give up on a silent station.** Await an incoming connection with a short,
   linked cancellation deadline. No client connects. Check for a canceled
   terminal completion before releasing the listener. The application context
   remains usable because the short deadline belongs to a separate source.

The native `BoundPort` helper is necessary because the current socket API does
not expose a local-endpoint query. It only obtains the port assigned by the OS;
the actual network operations use NGIN APIs.

## Ownership and shutdown

Tasks are cold until spawned or awaited. Keep running operations owned and
observe their terminal results. `WhenAll` waits for every child, including
when one fails; the shared deadline cancels a peer that would otherwise keep
waiting. Named coroutine functions keep their parameters in their frames and
avoid dangling temporary coroutine-lambda captures.

The shutdown order is deliberate:

1. Finish work, or request cancellation and still await completion.
2. Release sockets, buffers, and file handles after their operations finish.
3. Remove the example's scratch directory.
4. Stop/destroy the I/O runtime, which joins its owned network thread.
5. Destroy the task scheduler.

`IO::Runtime::Stop()` stops polling and rejects new async work; it is not an application task-draining
operation. Dropping an `Operation` is also not a join. `IO::Runtime` expects callers
to finish their operations before its destructor runs, as the demos do.
Cleanup of the unique scratch directory is also attempted on failure.

## Try changing it

- Add a third delayed sensor and include its result in the mission log.
- Send several framed messages, updating both client and server loops.
- Change the silent station's deadline and observe cancellation independently
  of the application deadline.
- Set `.files = {.backendPreference = IO::Runtime::FileBackendPreference::Fallback}`
  in the runtime options to try the portable worker path explicitly.

Continue with the [shared runtime guide](../../Dependencies/NGIN/NGIN.Base/docs/IORuntime.md)
and the Base guides for [async tasks](../../Dependencies/NGIN/NGIN.Base/docs/Async.md),
[filesystem I/O](../../Dependencies/NGIN/NGIN.Base/docs/IO.md), and
[networking](../../Dependencies/NGIN/NGIN.Base/docs/Network.md).
