# Hello.IO

Three application entry patterns for async tasks, filesystem I/O, and networking.
Each runs the same local expedition: collect timed sensor readings, save and copy
a mission log, exchange it over loopback TCP, and cancel a silent listener.
No server, Internet connection, or NGIN.Core host is required.

## Run it

From the repository root:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug
./build/dev/Tools/NGIN.CLI/ngin build --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
./build/dev/Tools/NGIN.CLI/ngin run --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
./build/dev/Tools/NGIN.CLI/ngin test --project Examples/Hello.IO/Hello.IO.nginproj --configuration Debug --output build/manual/Hello.IO
```

Use `ngin.exe` on Windows. See [repository setup](../../README.md#try-ngin) to
build the CLI. The executable checks its results and returns nonzero on failure.
The smoke test has a 20-second process timeout; each expedition has a cooperative
10-second cancellation deadline.

## Three execution modes

The named entry functions in [main.cpp](src/main.cpp) make thread ownership and
continuation choice explicit. [Application.hpp](src/Application.hpp) owns only
the runtime and its bound filesystem; constructing it starts no threads.

| Entry | Runtime driver | Continuations | Application join |
| --- | --- | --- | --- |
| `ManualEntry` | `RunTask` on main | Runtime executor | Root helper joins its scope and shuts down |
| `BackgroundEntry` | `RuntimeRunner` | Runtime executor on runner thread | Main uses `SyncWait`, then runner joins |
| `ExternalEntry` | `RuntimeRunner` | Explicit external task pool | Main joins tasks before runner/pool destruction |

`ManualEntry` passes a root factory to `IO::RunTask`. The factory receives a
TaskContext and TaskScope; the helper owns root completion, child cancellation
and joining, and runtime shutdown. It returns a `Completion<T, E>`. There is one
application event-loop thread; filesystem operations that need blocking calls
use lazy workers.

`BackgroundEntry` creates a runner before starting tasks. Main may block in
SyncWait because the runner independently drives the required loop. It also
starts an accept with no incoming client, calls RequestStop from a task, and
joins that accept through its already reserved continuation while stopping.
New tasks and I/O are rejected after stop; existing joins continue.

`ExternalEntry` supplies a task-pool executor to TaskContext while sockets and
files remain bound to the same runtime. Completion transfers back to that pool.
`AnalyzeOnWorker` performs CPU work and verifies that it is running on the
selected external executor, keeping the I/O loop available for progress.

Never call blocking SyncWait from the only event-loop thread. Inside coroutines,
use co_await. A runner cannot take over a runtime already driven by another
thread: first drive fixes ownership for the runtime's lifetime.

## The expedition

- `GatherTelemetry` awaits two sensors through WhenAll. Timers suspend tasks
  without sleeping an application worker.
- `SaveMissionLog` writes, copies, reads back, and verifies the complete log.
  The coroutine owns the string while its write borrows the bytes. Native file
  completion is used where available; unsupported operations use lazy workers.
- `ExchangeWithMissionControl` starts server and client children in TaskScope.
  A failed peer requests sibling cancellation, and both join before the listener
  or borrowed log leaves scope. Retained child factories preserve their captures.
- TCP uses TcpByteStream and LengthPrefixedMessageStream for framing and partial
  transfers. Receive storage is bounded to 4096 bytes. Accepted sockets and
  adapters preserve the listener's runtime binding.
- `CancelSilentStation` uses a short linked cancellation deadline and waits for
  terminal completion before releasing its listener. The application context
  remains usable afterward.

The small native BoundPort helper reads the OS-selected loopback port because
SocketHandle has no local-endpoint query. Network operations themselves use NGIN.
Each mode creates a unique scratch directory and removes it after joining. Cleanup
is also attempted on failure.

## Ownership and host integration

TaskScope owns child operations, contexts, and factories. It does not make borrowed
root-local data live beyond its C++ scope: join those children before leaving the
scope, as the TCP exchange does. Dropping an Operation is not a join. Runtime
shutdown can transfer completions to an external executor before their application
tasks consume them; keep that executor and all borrowed resources alive until the
tasks finish. Destroying a scope with unfinished children terminates rather than
blocking its only driver or silently detaching work.

For a POSIX host-owned loop, call PollOnce until it reports no immediately ready
work, then obtain CopyNativeWaitSources and NextDeadline. Monitor every returned
read/write descriptor and arm a monotonic host timer. Pump and refresh on either
notification; new work and earlier deadlines wake the host without periodic
polling. The portable backend includes its wake pipe and all registered sockets;
Linux and macOS expose one aggregate descriptor. Allocate the reusable source
buffer once with registrationCapacity + 1 entries. Never consume or close these
borrowed descriptors yourself. Ownership stays on the host thread.

For Windows UI integration, keep RuntimeRunner driving I/O and choose the UI's
executor in TaskContext. Its submission and reserved-completion paths must notify
the UI's normal message queue. Keep pumping that executor while tasks cancel and
join; blocking SyncWait on it would deadlock. Do not wait on or dequeue from the
runtime's IOCP directly. CopyNativeWaitSources explicitly reports unsupported on
Windows. Native Windows execution will be validated in a Windows environment.

Continue with the [shared runtime guide](../../Dependencies/NGIN/NGIN.Base/docs/IORuntime.md),
[async ownership guide](../../Dependencies/NGIN/NGIN.Base/docs/Async.md#owned-child-tasks),
[filesystem guide](../../Dependencies/NGIN/NGIN.Base/docs/IO.md), and
[networking guide](../../Dependencies/NGIN/NGIN.Base/docs/Network.md).
