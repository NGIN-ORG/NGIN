---
title: NGIN.Base quick start
description: Add NGIN.Base to a product and run a small explicit asynchronous task.
---

# NGIN.Base quick start

This example creates a cold task, yields once through its explicit context, and
returns a value.

## Before you start

You need the NGIN CLI, a C++23 compiler, CMake, and a workspace that can resolve
the `NGIN.Base` package. Create this layout:

```text
BaseDemo/
├── BaseDemo.nginproj
└── src/
    └── main.cpp
```

## Add the package

```xml
<Executable Name="BaseDemo">
  <Uses>
    <Package Name="NGIN.Base" Version="0.1" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

## Create a task

```cpp
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>

enum class DemoError {
    InvalidInput,
};

NGIN::Async::Task<int, DemoError>
Compute(NGIN::Async::TaskContext& context) {
    co_await context.YieldNow();
    co_return 7;
}

int main() {
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext context{scheduler};

    auto operation = NGIN::Async::Spawn(context, Compute(context));
    scheduler.RunUntilIdle();

    auto result = operation.TakeResult();
    return result && result.Value() == 7 ? 0 : 1;
}
```

Tasks are cold. Creating `Compute` does not start background work. `Spawn`
starts it under an explicit context, the scheduler drives it, and the root
operation owns the terminal result. This makes runtime and cancellation
behavior visible at the call site.

## Build the product

```bash
ngin validate --project BaseDemo/BaseDemo.nginproj --configuration Debug
ngin build --project BaseDemo/BaseDemo.nginproj --configuration Debug
ngin run --project BaseDemo/BaseDemo.nginproj --configuration Debug
```

The program prints nothing and exits `0`. Exit `1` means the task did not
complete successfully with value `7`.

## If it fails

- Package resolution failure: run `ngin graph --format json` with the same
  selection and confirm the workspace discovers the NGIN.Base package.
- Task never completes: keep the scheduler/context alive and drive the
  cooperative scheduler.
- Missing async symbol/header: confirm the Execution component/export is
  active; Async belongs to `NGIN::Base::Execution`.

## Choose a narrower surface

When consuming NGIN.Base through CMake directly, prefer a component target such
as `NGIN::Base::IO` or `NGIN::Base::Crypto` if that is all the target uses. The
aggregate `NGIN::Base` target remains convenient for applications spanning
several areas.

Continue with [async and execution](./async-execution.md) or return to the
[subsystem map](../base.md). Use the [Async C++ reference](../../reference/cpp/base/async/index.md)
for exact task and completion contracts.
