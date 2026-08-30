---
title: NGIN.Log quick start
description: Create a console logger and add structured attributes without macros.
---

# NGIN.Log quick start

## Before you start

Add the `NGIN.Log` package to an executable product and compile the example as
`src/main.cpp`. No global logger initialization or macro definition is needed.

```xml
<Executable Name="LogDemo">
  <Uses>
    <Package Name="NGIN.Log" Version="0.1" />
  </Uses>
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

## Create a console logger

```cpp
#include <NGIN/Log/Log.hpp>

int main() {
    using Logger = NGIN::Log::Logger<NGIN::Log::LogLevel::Trace>;

    Logger::SinkSet sinks;
    sinks.push_back(NGIN::Log::MakeSink<NGIN::Log::ConsoleSink>());

    Logger logger{"App", NGIN::Log::LogLevel::Info, std::move(sinks)};
    logger.Info("application started");
    logger.Warn("slow request");
}
```

The template level removes calls below the compiled level. The runtime level
filters the remaining calls for this logger.

## Add structured attributes

```cpp
logger.Info([&](NGIN::Log::RecordBuilder& record) {
    record.Message("request finished");
    record.Attr("status", 200);
    record.Attr("bytes", 1536);
});
```

The builder callback runs only when compile-time and runtime filtering allow
the record. Use it for structured fields or expensive message construction.

## Check the result

Run the product. The console should contain `application started`, `slow
request`, and `request finished`. A `Debug` message would be filtered because
the runtime minimum is `Info` even though the template permits `Trace`.

## If it fails

- No output: confirm the sink set was moved into the logger and the runtime
  minimum permits the call.
- Expensive work still runs: construct it inside the builder callback, not
  before calling the logger.
- Missing context after thread migration: scoped context is thread-local;
  propagate the fields explicitly.
- Production losses/errors: inspect async sink drop and sink-error counters.

Continue with [records and formatting](./records-formatting.md) or use the
[NGIN.Log C++ reference](../../reference/cpp/log/index.md).
