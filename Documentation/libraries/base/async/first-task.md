---
title: Your first async operation
description: Build, run, and inspect a complete NGIN.Async task without assuming prior coroutine experience.
---

# Your first async operation

You will build a program that starts two child tasks, awaits them in sequence,
and prints their sum. This uses a caller-driven scheduler so every execution
step is visible.

## Before you start

You need C++23 and the NGIN.Base Execution component. With CMake:

```cmake
find_package(NGINBase CONFIG REQUIRED COMPONENTS Execution)
target_link_libraries(AsyncDemo PRIVATE NGIN::Base::Execution)
target_compile_features(AsyncDemo PRIVATE cxx_std_23)
```

In an NGIN product, select the `NGIN.Base` package and compile the source as in
the [Base quick start](../quick-start.md).

## Complete program

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

#include <iostream>

enum class MathError {
    NegativeInput,
};

NGIN::Async::Task<int, MathError>
DoubleAfterYield(NGIN::Async::TaskContext& context, int value) {
    if (value < 0) {
        co_return NGIN::Async::Completion<int, MathError>::DomainFailure(
            MathError::NegativeInput);
    }

    co_await context.YieldNow();
    co_return value * 2;
}

NGIN::Async::Task<int, MathError>
Calculate(NGIN::Async::TaskContext& context) {
    int first = co_await DoubleAfterYield(context, 10);
    int second = co_await DoubleAfterYield(context, 5);
    co_return first + second;
}

int main() {
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext context {scheduler};

    auto operation = NGIN::Async::Spawn(context, Calculate(context));
    scheduler.RunUntilIdle();

    auto result = operation.TakeResult();
    if (!result) {
        std::cerr << "calculation failed\n";
        return 1;
    }

    std::cout << result.Value() << '\n';
    return 0;
}
```

Expected output:

```text
30
```

## Read the program from the inside out

`DoubleAfterYield` is a coroutine because it uses `co_await` and `co_return`.
Its return type declares both outcomes the caller can use:

```cpp
Task<int, MathError>
     │       │
     │       └─ expected domain-error type
     └─ success value type
```

`Calculate` awaits each child. It receives an `int` on success. It does not
need to manually unwrap a child `Completion`: the task awaiter propagates any
non-success to `Calculate`.

`main` is not a coroutine, so it creates the execution context, starts root
work with `Spawn`, drives the cooperative scheduler, and consumes the final
completion.

## Prove that tasks are cold

Add a print as the first line of `Calculate`, then change `main` temporarily:

```cpp
auto task = Calculate(context);
std::cout << "task created\n";
```

Only `task created` prints. Restore the `Spawn` call and the coroutine body
runs. This matters when a function allocates resources or captures references:
calling it constructs coroutine state, but execution begins at an explicit
start/await boundary.

## Observe one scheduler step at a time

Replace `RunUntilIdle` with:

```cpp
while (!operation.IsCompleted()) {
    const bool ranWork = scheduler.RunOne();
    if (!ranWork) {
        std::cerr << "no ready work can make progress\n";
        return 2;
    }
}
```

This is the shape used by an application loop that owns other work between
async resumptions. `RunOne()` returning `false` means no item was ready at that
moment. A future timer may still exist; a real loop waits until its deadline or
another event.

## Trigger a domain error

Change the first input to `-10`. `DoubleAfterYield` returns
`MathError::NegativeInput`. That state propagates through `Calculate` and the
root completion becomes a domain error.

Inspect it explicitly:

```cpp
if (result.IsDomainError() &&
    result.DomainError() == MathError::NegativeInput) {
    std::cerr << "input must not be negative\n";
    return 3;
}
```

## What this example does not hide

- There is no global scheduler.
- No thread was created.
- The task was started once and consumed once.
- The domain error is part of the C++ type.
- The context and scheduler outlive every task using them.

## Next

Read [errors and completions](./errors.md), then
[contexts and schedulers](./runtime.md). For exact declarations, open the
[`Task<T, E>` reference](../../../reference/cpp/base/async/task.md).

