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
File backend: worker fallback
[async] Both sensors checked in
Captain's log: the async expedition
Shields: 98%
Emergency cookies: 7
[files] Mission log saved, copied, and verified
[tcp] Mission control echoed the complete log
[cancel] Silent station wait canceled and completed
Hello.IO complete: scratch files removed, all tasks joined
```

## Application setup

Start with [`src/Runtime.hpp`](src/Runtime.hpp). The application owns these
resources once and shares them across operations:

| Resource | Responsibility |
| --- | --- |
| `ThreadPoolScheduler` | Executes coroutine continuations and timers; accepts completion submissions from I/O threads |
| `TaskContext` | Borrows that scheduler and carries cancellation into tasks |
| Shared `FileSystemDriver` | Owns native file I/O machinery and fallback workers |
| `LocalFileSystem` | Uses that explicitly supplied file driver |
| `NetworkDriver` | Tracks socket readiness/completion for all sockets |
| Dedicated network thread | Calls `NetworkDriver::Run()` until shutdown |

The task scheduler uses one worker, enough for these mostly-waiting tasks.
Schedulers also have timer infrastructure; this is not a promise of only one
thread for the whole application. Filesystem work has separate workers.
`workerThreads = 0` on the network driver means its `Run()` loop executes on
our dedicated thread. `Create()` alone does not start polling, and increasing
`workerThreads` does not turn `Run()` into a nonblocking start function.

All demos use the same application task context. They do not run their whole
coroutines on the network driver or use a special filesystem task context.
The main thread calls `SyncWait` only at application boundaries. Coroutine
functions use `co_await`; blocking an executor worker could prevent progress.

The current `CooperativeScheduler` does not synchronize cross-thread queue
access. Do not substitute it here while background I/O threads submit to it.
An application integrating polling into its own loop must choose a scheduler
and completion-dispatch arrangement appropriate to that threading model.

## The expedition

Read the named functions in [`src/main.cpp`](src/main.cpp):

1. **Gather telemetry.** Two simulated sensors await different timers.
   `WhenAll` starts both tasks and joins them. Timers suspend tasks without
   sleeping a worker thread.
2. **Save the black box.** Write the telemetry asynchronously, copy it to a
   backup, read it back, and verify every byte. The coroutine owns the string
   while the write operation borrows its byte span. `LocalFileSystem` uses
   the shared driver's native backend when available, otherwise its fallback.
3. **Contact mission control.** Run a TCP listener and client concurrently on
   loopback. Send the saved log and verify the echo. `TcpByteStream` owns its
   socket and borrows the driver once; `LengthPrefixedMessageStream` handles
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
4. Stop the network loop and join its thread.
5. Destroy the I/O drivers, then the task scheduler.

`NetworkDriver::Stop()` stops polling; it is not an application task-draining
operation. Dropping an `Operation` is also not a join. `Runtime` expects callers
to finish their operations before its destructor runs, as the demos do.
Cleanup of the unique scratch directory is also attempted on failure.

## Try changing it

- Add a third delayed sensor and include its result in the mission log.
- Send several framed messages, updating both client and server loops.
- Change the silent station's deadline and observe cancellation independently
  of the application deadline.
- Select `FileSystemDriver::BackendPreference::Fallback` in `Runtime` to try
  the portable worker path explicitly.

Continue with the Base guides for [async tasks](../../Dependencies/NGIN/NGIN.Base/docs/Async.md),
[filesystem I/O](../../Dependencies/NGIN/NGIN.Base/docs/IO.md), and
[networking](../../Dependencies/NGIN/NGIN.Base/docs/Network.md).
