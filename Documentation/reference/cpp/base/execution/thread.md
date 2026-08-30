---
title: Thread and WorkerThread
description: Code reference for native thread ownership, options, destruction policy, naming, and current-thread functions.
---

# `Thread` and `WorkerThread`

**Header:** `<NGIN/Execution/Thread.hpp>`  
**Namespace:** `NGIN::Execution`  
**Target:** `NGIN::Base::Execution`  
**Defined:** [`Thread.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Execution/Thread.hpp#L22)

## Declaration summary

```cpp
class Thread {
public:
    enum class OnDestruct : UInt8 { Join, Detach, Terminate };

    struct Options {
        ThreadName name;
        UInt64 affinityMask;
        int priority;
        UIntSize stackSize;
        OnDestruct onDestruct;
    };

    Thread() noexcept;
    template<typename F> explicit Thread(F&& entry, Options = {});
    ~Thread() noexcept;

    void Start(Utilities::Callable<void()> entry, Options = {});
    void Join() noexcept;
    void Detach() noexcept;
    bool IsJoinable() const noexcept;
    ThreadId GetId() const noexcept;
    NativeHandle NativeHandleValue() noexcept;
    bool SetName(ThreadName) noexcept;
    bool SetAffinity(UInt64) noexcept;
    bool SetPriority(int) noexcept;
};
```

`Thread` is move-only. Starting an already joinable handle or using an empty
entry terminates. Destruction applies `Options::onDestruct`; the default is
`Terminate`. `WorkerThread` wraps the same surface but forces `Join` policy.

## `ThreadName`

`ThreadName` stores at most `MaxBytes` in fixed inline storage. Longer input is
truncated. `Empty`, `Size`, `View`, and `CStr` inspect it. Platform name-setting
can still fail and reports `false`.

## `ThisThread`

```cpp
std::uint32_t HardwareConcurrency() noexcept;
ThreadId GetId() noexcept;
void YieldNow() noexcept;
void RelaxCpu() noexcept;
template<typename TUnit> void SleepFor(const TUnit&) noexcept;
void SleepUntil(Time::TimePoint) noexcept;
bool SetName(std::string_view) noexcept;
bool SetAffinity(UInt64) noexcept;
bool SetPriority(int) noexcept;
```

Affinity and priority values are platform-defined hints/contracts. Check the
Boolean result and avoid assuming identical behavior across supported systems.

