---
title: Process and dynamic-library API
description: API reference for launching child processes and resolving symbols from shared libraries.
---

# Process and dynamic-library API

## `Process`

**Headers:** `<NGIN/IO/Process.hpp>`, `<NGIN/IO/ProcessOptions.hpp>`

```cpp
static ProcessExpected<Process> Process::Start(ProcessOptions options);
bool IsValid() const noexcept;
bool IsRunning() const noexcept;
ProcessExpected<ProcessResult> Wait();
ProcessExpected<void> Terminate();

ProcessExpected<ProcessResult> RunProcess(ProcessOptions options);
Async::Task<ProcessResult, ProcessError> RunProcessAsync(
    Async::TaskContext&, ProcessOptions);
```

`ProcessOptions` carries executable, arguments, working directory,
environment, and `ProcessStreamOptions`. `ProcessStreamMode` chooses inherited,
discarded, captured, or otherwise configured standard streams. `ProcessResult`
separates exit information and captured output from launch/transport failures.

## `DynamicLibrary`

**Header:** `<NGIN/IO/DynamicLibrary.hpp>`

`DynamicLibrary::Open(Path|string_view, LoadMode)` returns a move-only loaded
library or throws `DynamicLibraryError`. `Resolve<T>` returns a typed symbol and
throws when missing; `TryResolve<T>` returns `std::optional<T>`. `IsLoaded`,
`GetPath`, and `GetLoadMode` inspect state.

The library must remain loaded while any resolved pointer is used. Symbol type
correctness and ABI compatibility are caller responsibilities.

**Defined:** [`Process.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO/Process.hpp), [`DynamicLibrary.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/IO/DynamicLibrary.hpp)
