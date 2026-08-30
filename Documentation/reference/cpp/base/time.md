---
title: Time API
description: API reference for monotonic TimePoint, MonotonicClock, duration conversion, and SleepFor.
---

# Time API

**Header:** `<NGIN/Time.hpp>`  
**Namespace:** `NGIN::Time`

## `TimePoint`

```cpp
struct TimePoint final {
    static constexpr TimePoint FromNanoseconds(UInt64) noexcept;
    constexpr UInt64 ToNanoseconds() const noexcept;
    // equality and total ordering operators
};
```

The value represents nanoseconds since an unspecified epoch in the monotonic
clock domain. It has no calendar-time conversion.

## Clock and sleep

```cpp
struct MonotonicClock final {
    static TimePoint Now() noexcept;
};

template<typename TUnit>
UInt64 ToNanosecondsCeil(const TUnit& duration) noexcept;

template<typename TUnit>
void SleepFor(const TUnit& duration) noexcept;
```

The duration templates require a NGIN unit with time dimension.
`ToNanosecondsCeil` returns zero for non-positive input and rounds positive
fractional nanoseconds upward. The caller must keep the converted value within
the representable `UInt64` range. `SleepFor` blocks the current thread using
that conversion.

**Defined:** [`TimePoint.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Time/TimePoint.hpp), [`Sleep.hpp`](https://github.com/NGIN-ORG/NGIN/blob/main/Dependencies/NGIN/NGIN.Base/include/NGIN/Time/Sleep.hpp)
