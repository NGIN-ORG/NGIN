---
title: Processes and dynamic libraries
description: Start and supervise child processes and load native libraries with explicit handle, stream, symbol, and ABI lifetime.
---

# Processes and dynamic libraries

## Processes

`ProcessOptions` carries executable, argument list, working directory,
environment, standard-stream modes, and supervision policy. Pass arguments as
separate values; never concatenate untrusted input into a shell command.

```cpp
auto started = NGIN::IO::Process::Start(std::move(options));
if (!started) {
    return Report(started.error());
}

auto result = started->Wait();
```

`Process` is a move-only platform handle. `IsRunning`, `Wait`, and `Terminate`
manage it. `RunProcess` combines start+wait; `RunProcessAsync(context, options)`
returns `Task<ProcessResult, ProcessError>`.

Launch success and child exit success are distinct. Inspect `ProcessResult`
exit/termination and captured stdout/stderr separately. Define output limits
and avoid deadlock by using the API’s capture/drain policy rather than filling
an unread child pipe.

## Dynamic libraries

`DynamicLibrary` is move-only RAII. Construction/Open may load now or lazily;
`Load` and required `Resolve<T>` throw `DynamicLibraryError`, while
`TryResolve<T>` returns optional and `Unload` is best-effort `noexcept`.

Resolved pointers become invalid immediately after unload/destruction. Stop
callbacks/threads, destroy objects whose code/destructors reside in the module,
then unload. Symbol presence does not prove compiler, runtime, layout, calling
convention, or plugin ABI compatibility.

